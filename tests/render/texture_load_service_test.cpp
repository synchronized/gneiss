// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/codec_result.hpp"
#include "../support/memory_file_fixture.hpp"
#include "../support/prepare_render.hpp"
#include "../support/value_result.hpp"
#include <gneiss/engine/error.hpp>
#include <gneiss/engine/core/serialization/json.hpp>
#include <gneiss/engine/core/tasks/task_scheduler.hpp>
#include <gneiss/engine/function/asset/asset_manager.hpp>
#include "function/asset/detail/asset_construction.hpp"
#include <gneiss/engine/function/meta_registration.hpp>
#include <gneiss/engine/function/render/service.hpp>
#include <gneiss/engine/platform/files/file_backend.hpp>
#include <gneiss/engine/platform/files/file_service.hpp>
#include <gneiss/engine/resource/material/material_description.hpp>
#include <gneiss/engine/resource/texture/texture_ktx2.hpp>
#include <gneiss/engine/resource/mesh/mesh_decoder.hpp>
#include <algorithm>
#include <stdexcept>

#include <atomic>
#include <cstdio>
#include <map>
#include <source_location>

namespace {
using namespace gneiss;
using namespace gneiss;
void check(bool value, std::source_location at = std::source_location::current()) {
  if (!value) {
    throw std::runtime_error("异步资产契约失败，行=" + std::to_string(at.line()));
  }
}
struct memory_files final : gneiss::test_support::memory_file_fixture {
  std::map<std::string, std::vector<std::byte>> files;
  mutable std::thread::id reader;
  std::shared_ptr<std::atomic_size_t> bytes_read = std::make_shared<std::atomic_size_t>();
  gneiss::error read(std::string_view path,
                             std::vector<std::byte>& bytes) const noexcept override {
    return read_bounded(path, SIZE_MAX, bytes);
  }
  gneiss::error read_bounded(std::string_view path, std::size_t limit,
                                     std::vector<std::byte>& bytes) const noexcept override {
    reader = std::this_thread::get_id();
    const auto found = files.find(std::string(path));
    if (found == files.end()) {
      return gneiss::error::not_found;
    }
    if (found->second.size() > limit) {
      return gneiss::error::invalid_argument;
    }
    bytes = found->second;
    bytes_read->fetch_add(bytes.size(), std::memory_order_relaxed);
    return gneiss::error::success;
  }
  void text(std::string path, std::string_view text) {
    auto& bytes = files[std::move(path)];
    bytes.clear();
    for (const auto c : text) {
      bytes.push_back(static_cast<std::byte>(c));
    }
  }
  void pixel(std::byte red) {
    std::string diagnostic;
    check(gneiss::test_support::take_codec(
              encode_texture_ktx2(
                  {.transfer = texture_transfer::srgb,
                   .levels = {{.width = 1U,
                               .height = 1U,
                               .pixels = {red, std::byte{}, std::byte{}, std::byte{255}}}}}),
              files["image.ktx2"], diagnostic) == std::nullopt);
  }
};
// 人工延迟后端只接受回调；用于验证文件等待与 CPU 调度完全分离。
class delayed_backend final : public gneiss::files::backend {
public:
  std::string path;
  gneiss::files::callback<gneiss::files::bytes> callback;
  std::size_t reads{};
  error read(std::string_view requested, gneiss::files::read_options,
             gneiss::files::callback<gneiss::files::bytes> completed) override {
    check(!callback);
    path = requested;
    callback = std::move(completed);
    ++reads;
    return error::success;
  }
  void complete(gneiss::files::bytes bytes, error status = error::success) {
    auto completed = std::move(callback);
    check(static_cast<bool>(completed));
    completed({.status = status, .data = std::move(bytes)});
  }
};
void texture_source_revisions() {
  auto source = std::make_shared<memory_files>();
  source->pixel(std::byte{10});
  source->files["payload/image.ktx2"] = source->files.at("image.ktx2");
  source->text(
      "texture",
      R"({"format":"gneiss.texture","version":2,"source":"asset://payload/image.ktx2","color_space":"srgb","usage":"default"})");
  gneiss::test_support::fixture_files files;
  check(files.mount("asset://", source).ok());
  // 来源摘要属于解码记录；直接验证纹理描述及载荷，不构造递归依赖批次。
  const render_resource_source request{"asset://texture", render_resource_kind::texture};
  decoded_asset before, repeated, after;
  asset_diagnostic diagnostic;
  check(gneiss::test_support::decode_fixture(files, request, before, diagnostic, SIZE_MAX,
                                             SIZE_MAX, {}).ok());
  check(gneiss::test_support::decode_fixture(files, request, repeated, diagnostic, SIZE_MAX,
                                             SIZE_MAX, {}).ok());
  check(before.sources.size() == 2 && repeated.sources.size() == 2);
  check(before.sources.back().digest == repeated.sources.back().digest);
  source->pixel(std::byte{20});
  source->files["payload/image.ktx2"] = source->files.at("image.ktx2");
  check(gneiss::test_support::decode_fixture(files, request, after, diagnostic, SIZE_MAX,
                                             SIZE_MAX, {}).ok());
  check(after.sources.size() == 2);
  check(before.uri == after.uri && before.sources.front().identity == after.sources.front().identity);
  check(before.sources.front().digest == after.sources.front().digest);
  const auto& payload = after.sources.back();
  asset_identity expected;
  check(files.identify("asset://payload/image.ktx2",
                       static_cast<std::uint32_t>(render_resource_kind::texture), expected).ok());
  check(payload.identity == expected && payload.identity == before.sources.back().identity);
  check(payload.bytes == source->files.at("image.ktx2").size());
  check(payload.digest == gneiss::sha256(source->files.at("image.ktx2")));
  check(payload.digest != before.sources.back().digest);
}
void programmatic_texture_content() {
  gneiss::render::service render;
  detail::asset_construction factory{render};
  std::array<std::uint8_t, 12> pixels{1, 2, 3, 4, 99, 99, 99, 99, 5, 6, 7, 8};
  texture_view view{.width = 1, .height = 2, .row_stride_bytes = 8, .pixels = pixels};
  auto created = factory.create_texture(view);
  check(bool(created));
  auto content = render.share_texture(resource_handle(*created));
  check(content && content->levels.front().pixels.size() == 8 &&
        content->levels.front().pixels[4] == std::byte{5} && (*created)->metadata()->height == 2);
  pixels[0] = 42;
  check(content->levels.front().pixels[0] == std::byte{1});
  view.row_stride_bytes = 3;
  auto invalid = factory.create_texture(view);
  check(!invalid && invalid.error() == error::invalid_argument);
  auto material = factory.create_material({}, {*created});
  check(bool(material));
  std::weak_ptr<const texture> texture_lifetime = created->shared();
  std::weak_ptr<const material_content> cpu_lifetime = (*material)->snapshot().shared();
  const auto texture_handle = resource_handle(*created);
  created = error::invalid_state;
  check(!texture_lifetime.expired());
  material = error::invalid_state;
  // 未消费 RID 队列前，CPU 材质及其纹理强引用已析构。
  check(cpu_lifetime.expired() && texture_lifetime.expired());
  check(render.texture_status(texture_handle) != error::invalid_handle);
  check(factory.drain() == 2);
  check(render.texture_status(texture_handle) == error::invalid_handle);
}

void mesh_data_lifetime() {
  auto source = std::make_shared<memory_files>();
  const std::string text =
      R"({"format":"gneiss.mesh","version":3,"topology":"triangle_list","vertices":[[0,0,0],[1,0,0],[0,1,0]],"uvs":[[0,0],[1,0],[0,1]],"normals":[[0,0,1],[0,0,1],[0,0,1]]})";
  source->text("mesh", text);
  // 这是 CPU 数据移动与渲染快照生命周期测试，直接解码，不构造资产加载批次。
  auto decoded = gneiss::detail::parse_mesh(source->files.at("mesh"));
  check(bool(decoded));
  const auto* vertices = decoded->vertices.data();
  auto mesh = std::make_shared<const mesh_data>(std::move(*decoded));
  check(mesh->vertices.data() == vertices);
  gneiss::render::service resources;
  gneiss::detail::asset_construction resources_factory{resources};
  {
    mesh_view view{.vertices = mesh->vertices,
                   .normals = mesh->normals,
                   .indices = mesh->indices,
                   .tangents = mesh->tangents,
                   .uv1 = mesh->uv1,
                   .colors = mesh->colors};
    auto created = resources_factory.create_mesh(view);
    check(bool(created) && (*created)->metadata()->vertices == mesh->vertices.size() &&
          (*created)->metadata()->has_normals);
    auto content = resources.share_mesh(resource_handle(*created));
    check(content && content->vertices.data() != mesh->vertices.data() &&
          content->vertices[1].position.x == mesh->vertices[1].position.x);
    view.vertices = {};
    auto invalid = resources_factory.create_mesh(view);
    check(!invalid && invalid.error() == error::invalid_argument);
  }
  check(resources_factory.drain() == 1);
  gneiss::rid rid{};
  check(gneiss::test_support::assign_value(resources.create_prepared_mesh(mesh), rid) ==
        gneiss::error::success);
  check(resources.share_mesh(rid) == mesh);
  auto frame = resources.share_mesh(rid);
  check(resources.destroy_mesh(rid) == gneiss::error::success &&
        !resources.share_mesh(rid));
  std::weak_ptr<const mesh_data> lifetime = mesh;
  mesh.reset();
  check(!lifetime.expired() && frame->vertices[1].position.x == 1.0F);
  frame.reset();
  check(lifetime.expired());
}
void independent_resource_identity() {
  gneiss::test_support::fixture_files files;
  check(files.mount("asset://", std::make_shared<memory_files>()) ==
        gneiss::error::success);
  gneiss::render::service resources;
  gneiss::detail::asset_construction resources_factory{resources};
  gneiss::asset_manager asset_owner(gneiss::test_support::test_files(files));
  [[maybe_unused]] auto& cache = asset_owner;
  auto& loader = asset_owner;
  if (loader.attach_render(resources).failed())
    throw std::runtime_error("资源管理器渲染绑定失败");
  const render_resource_source source_a{"asset://a", render_resource_kind::material};
  const render_resource_source source_b{"asset://b", render_resource_kind::material};
  render_resource_ref a, b;
  check(gneiss::test_support::take_value(gneiss::test_support::request_resource(loader, source_a),
                                         a) == gneiss::error::success);
  check(gneiss::test_support::take_value(gneiss::test_support::request_resource(loader, source_b),
                                         b) == gneiss::error::success);
  gneiss::test_support::fixture_files different;
  check(different.mount("asset://", std::make_shared<memory_files>()) ==
        gneiss::error::success);
  asset_manager isolated_owner(gneiss::test_support::test_files(different));
  auto& isolated = isolated_owner;
  if (isolated.attach_render(resources).failed())
    throw std::runtime_error("资源管理器渲染绑定失败");
  render_resource_ref conflict;
  check(gneiss::test_support::take_value(gneiss::test_support::request_resource(isolated, source_a),
                                         conflict) == gneiss::error::success &&
        conflict != a && gneiss::resource_rid(conflict) != gneiss::resource_rid(a));
  check(!isolated.owns(a));
  auto& shared = loader;
  if (shared.attach_render(resources).failed())
    throw std::runtime_error("资源管理器渲染绑定失败");
  check(gneiss::test_support::take_value(gneiss::test_support::request_resource(shared, source_a),
                                         conflict) == gneiss::error::success &&
        gneiss::resource_rid(conflict) == gneiss::resource_rid(a));

  const auto adopt = [&](const auto& source, auto& target) {
    decoded_asset decoded;
    decoded.uri = source.uri;
    decoded.content =
        asset_ref<const material_description>{std::make_shared<const material_description>()};
    return gneiss::test_support::adopt_render_for_test(loader, std::move(decoded), target);
  };
  check(adopt(source_a, a).ok());
  check(adopt(source_b, b).ok());
  const auto retained = resources.share_material(resource_rid(a));
  decoded_asset invalid;
  invalid.uri = source_a.uri;
  check(gneiss::test_support::adopt_render_for_test(loader, invalid, a) == error::invalid_argument);
  invalid.content = asset_ref<const material_description>{};
  check(gneiss::test_support::adopt_render_for_test(loader, invalid, a) == error::invalid_argument);
  invalid.content = texture_data{};
  check(gneiss::test_support::adopt_render_for_test(loader, invalid, a) == error::invalid_argument);
  check(resources.share_material(resource_rid(a)) == retained);
}
void reserved_material_lifetime() {
  auto source = std::make_shared<memory_files>();
  gneiss::test_support::fixture_files files;
  check(files.mount("asset://", source) == gneiss::error::success);
  gneiss::render::service resources;
  gneiss::detail::asset_construction resources_factory{resources};
  gneiss::asset_manager asset_owner(gneiss::test_support::test_files(files));
  [[maybe_unused]] auto& cache = asset_owner;
  auto& loader = asset_owner;
  if (loader.attach_render(resources).failed())
    throw std::runtime_error("资源管理器渲染绑定失败");
  const render_resource_source request{"asset://material", render_resource_kind::material};
  source->text("material",
      R"({"format":"gneiss.material","version":6,"parameters":{"metallic":0,"roughness":1,"color":[1,1,1,1]},"texture_uris":["","","","",""]})");
  auto& tasks = gneiss::test_support::asset_tasks();
  check(loader.configure(tasks).ok());
  auto pending = loader.load(request);
  check(bool(pending));
  auto target = pending->resource();
  const auto rid = gneiss::resource_rid(target);
  check(resources.material_status(rid) == error::not_ready);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (pending->status() == error::not_ready) {
    check(std::chrono::steady_clock::now() < deadline);
    (void)tasks.run_ready();
    (void)loader.async_files()->dispatch_completions(64);
    loader.advance();
  }
  {
    auto completed = pending->take();
    check(completed && *completed == target);
    check(resources.material_status(rid) == gneiss::error::success);
  } // 释放领取结果；成功领取已释放请求持有的引用。

  target = {};
  loader.collect_unused();
  check(resources.material_status(rid) == gneiss::error::invalid_handle);
}
void cpu_loading(gneiss::tasks::execution_mode mode) {
  gneiss::tasks::task_scheduler scheduler({.mode = mode});
  auto backend = std::make_shared<delayed_backend>();
  auto files = std::make_shared<gneiss::files::service>(
      gneiss::files::service_options{.max_file_bytes = 64U * 1024U * 1024U});
  check(files->mount({.prefix = "asset://", .readers = {backend}, .writer = {}}).ok());
  gneiss::render::service resources;
  gneiss::detail::asset_construction resources_factory{resources};
  asset_manager manager(gneiss::test_support::test_files(files));
  auto& loader = manager;
  if (loader.attach_render(resources).failed())
    throw std::runtime_error("资源管理器渲染绑定失败");
  check(loader.configure(scheduler).ok());
  memory_files fixture;
  fixture.pixel(std::byte{42});
  fixture.text(
      "texture",
      R"({"format":"gneiss.texture","version":2,"source":"asset://image.ktx2","color_space":"srgb","usage":"default"})");
  asset_request<render_resource_ref> request;
  auto advance = [&] {
    (void)scheduler.run_ready();
    check(files->dispatch_completions(64).ok());
    manager.advance();
    std::this_thread::yield();
  };
  auto until = [&](auto predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!predicate()) {
      check(std::chrono::steady_clock::now() < deadline);
      advance();
    }
  };
  check(gneiss::test_support::take_value(loader.load<gneiss::texture>("asset://texture"),
                                         request)
            .ok());
  auto target = request.resource();
  asset_request<render_resource_ref> duplicate;
  check(gneiss::test_support::take_value(loader.load<gneiss::texture>("asset://texture"),
                                         duplicate)
            .ok());
  check(duplicate.resource() == target);
  until([&] { return bool(backend->callback); });
  check(resources.texture_status(resource_rid(target)) == error::not_ready);
  request.cancel(); // 另一个调用方仍在等待，不取消共享工作。
  backend->complete(fixture.files.at("texture"));
  until([&] { return bool(backend->callback); });
  check(backend->path == "image.ktx2");
  backend->complete(fixture.files.at("image.ktx2"));
  until([&] { return duplicate.status() != error::not_ready; });
  check(duplicate.status().ok());
  auto typed_request = manager.load<gneiss::texture>("asset://texture");
  check(bool(typed_request));
  auto typed = typed_request->take();
  check(bool(typed) && *typed == resource_cast<gneiss::texture>(target));
  auto found_typed = manager.find<gneiss::texture>("asset://texture");
  check(bool(found_typed) && *found_typed == *typed);
  asset_load_options mismatched;
  mismatched.texture_profile.sampled_transfer_formats[0] = true;
  check(manager.find<gneiss::texture>("asset://texture", mismatched).error() ==
        error::not_found);
  check(manager.load<gneiss::texture>("asset://texture", mismatched).error() ==
        error::invalid_argument);
  // 默认装配负载不在最终资产索引中。
  check(manager.find<texture_data>("asset://texture").error() == error::unsupported);
  const auto previous = resources.share_texture(resource_rid(target));
  check(previous && previous->levels.front().pixels.front() == std::byte{42});
  const auto reads = backend->reads;
  check(gneiss::test_support::take_value(loader.load<gneiss::texture>("asset://texture"),
                                         request)
            .ok());
  check(request.status().ok() && backend->reads == reads);
  check(gneiss::test_support::take_value(loader.reload<gneiss::texture>("asset://texture"),
                                         request)
            .ok());
  until([&] { return bool(backend->callback); });
  backend->complete({}, error::io);
  until([&] { return request.status() != error::not_ready; });
  check(request.status() == error::io && resources.share_texture(resource_rid(target)) == previous);
  check(gneiss::test_support::take_value(loader.reload<gneiss::texture>("asset://texture"),
                                         request)
            .ok());
  until([&] { return bool(backend->callback); });
  request.cancel();
  // 组件/上层仍持有 target：请求观察者释放后，加载继续且后续订阅复用同一身份。
  advance();
  backend->complete(fixture.files.at("texture"));
  until([&] { return bool(backend->callback); });
  auto retained_request = loader.load<gneiss::texture>("asset://texture");
  check(retained_request && retained_request->resource() == target);
  backend->complete(fixture.files.at("image.ktx2"));
  until([&] { return retained_request->status() != error::not_ready; });
  check(retained_request->status().ok() &&
        resources.share_texture(resource_rid(target)) != previous);
  const auto abandoned_reads = backend->reads;
  auto abandoned = loader.load<gneiss::texture>("asset://abandoned");
  check(bool(abandoned));
  abandoned->cancel();
  advance();
  check(backend->reads == abandoned_reads);
  fixture.text(
      "mesh",
      R"({"format":"gneiss.mesh","version":3,"topology":"triangle_list","vertices":[[0,0,0],[1,0,0],[0,1,0]],"uvs":[[0,0],[0,0],[0,0]],"normals":[[0.0,0.0,1.0],[0.0,0.0,1.0],[0.0,0.0,1.0]]})");
  fixture.text(
      "material",
      R"({"format":"gneiss.material","version":6,"parameters":{"metallic":0,"roughness":1,"color":[1,1,1,1]},"texture_uris":["asset://texture","","","",""]})");
  check(gneiss::test_support::take_value(loader.load<gneiss::material>("asset://material"),
                                         request)
            .ok());
  until([&] {
    if (backend->callback)
      backend->complete(fixture.files.at(backend->path));
    return request.status() != error::not_ready;
  });
  check(request.status().ok());
  check(request.get<gneiss::material>()->textures()[0] ==
        resource_cast<gneiss::texture>(target));
  check(gneiss::test_support::take_value(
            loader.reload({"asset://mesh", render_resource_kind::mesh}), request)
            .ok());
  until([&] {
    if (backend->callback)
      backend->complete(fixture.files.at(backend->path));
    return request.status() != error::not_ready;
  });
  check(request.status().ok());
  // 中间数据只由 Loader 使用，公开入口不能建立第二份缓存。
  manager.collect_unused();
  const auto cached = manager.cached_count();
  const auto reads_before_rejection = backend->reads;
  check(manager.load<mesh_data>("asset://mesh").error() == error::unsupported);
  check(manager.load<texture_data>("asset://texture").error() == error::unsupported);
  check(manager.load<material_description>("asset://material").error() == error::unsupported);
  check(manager.find<mesh_data>("asset://mesh").error() == error::unsupported);
  check(manager.find<material_description>("asset://material").error() == error::unsupported);
  check(manager.store<mesh_data>("asset://mesh", std::make_shared<const mesh_data>()).error() ==
        error::unsupported);
  check(manager.store<texture_data>("asset://texture", std::make_shared<const texture_data>())
            .error() == error::unsupported);
  check(manager
            .store<material_description>("asset://material",
                                         std::make_shared<const material_description>())
            .error() == error::unsupported);
  check(backend->reads == reads_before_rejection && manager.cached_count() == cached);

  auto mesh_request = manager.reload<gneiss::mesh>("asset://mesh");
  auto cancelled = manager.load<gneiss::mesh>("asset://mesh");
  check(mesh_request && cancelled);
  cancelled->cancel();
  auto joined = manager.load<gneiss::mesh>("asset://mesh");
  check(bool(joined));
  until([&] {
    if (backend->callback)
      backend->complete(fixture.files.at(backend->path));
    return mesh_request->status() != error::not_ready;
  });
  auto mesh_result = mesh_request->take();
  auto joined_result = joined->take();
  check(mesh_result && joined_result && *mesh_result == *joined_result);
  check(mesh_request->metrics().sources.size() == 1U);
  auto mesh_content = resources.share_mesh(resource_handle(*mesh_result));
  check(mesh_content && mesh_content->vertices.size() == 3U);
  check(manager.cached_count() == cached);

  fixture.files["cpu_texture"] = fixture.files.at("texture");
  auto texture_request = manager.load<gneiss::texture>("asset://cpu_texture");
  check(bool(texture_request));
  until([&] { return bool(backend->callback); });
  manager.request_stop();
  check(texture_request->status() == error::invalid_state);
  backend->complete(fixture.files.at("cpu_texture"));
  until([&] { return manager.stopped(); });
  gneiss::ref<const gneiss::texture> rejected;
  check(gneiss::test_support::take_value(manager.create(texture_view{}), rejected) ==
            error::invalid_state &&
        !rejected);
  check(manager.configure(scheduler) == error::invalid_state);
  check(manager.attach_render(resources) == error::invalid_state);
  check(manager.load<gneiss::texture>("asset://texture").error() == error::invalid_state);
  check(manager.load<gneiss::mesh>("asset://mesh").error() == error::invalid_state);
}
void independent_loads(gneiss::tasks::execution_mode mode) {
  gneiss::tasks::task_scheduler scheduler({.mode = mode});
  auto source = std::make_shared<memory_files>();
  source->text(
      "a",
      R"({"format":"gneiss.material","version":6,"parameters":{"metallic":0,"roughness":1,"color":[1,1,1,1]},"texture_uris":["","","","",""]})");
  source->text(
      "b",
      R"({"format":"gneiss.material","version":6,"parameters":{"metallic":0,"roughness":1,"color":[0,1,0,1]},"texture_uris":["","","","",""]})");
  source->text(
      "bad",
      R"({"format":"gneiss.material","version":6,"parameters":{"metallic":0,"roughness":1,"color":[1,1,1,1]},"texture_uris":["asset://absent","","","",""]})");
  source->text(
      "dependent",
      R"({"format":"gneiss.material","version":6,"parameters":{"metallic":0,"roughness":1,"color":[1,1,1,1]},"texture_uris":["asset://texture","","","",""]})");
  source->text(
      "texture",
      R"({"format":"gneiss.texture","version":2,"source":"asset://image.ktx2","color_space":"srgb","usage":"default"})");
  source->pixel(std::byte{42});
  test_support::fixture_files files;
  check(files.mount("asset://", source).ok());
  asset_manager manager(test_support::test_files(files));
  gneiss::render::service render;
  auto& service = manager;
  // 配置与 Render 绑定顺序不影响加载，且两者都不能在停止后重新装配。
  if (mode == gneiss::tasks::execution_mode::cooperative) {
    check(service.configure(scheduler).ok());
    check(service.attach_render(render).ok());
  } else {
    check(service.attach_render(render).ok());
    check(service.configure(scheduler).ok());
  }
  check(service.configure(scheduler).ok());
  gneiss::ref<const gneiss::material> found;
  const auto reads_before_find = source->bytes_read->load();
  check(gneiss::test_support::take_value(service.find<gneiss::material>("asset://a"),
                                         found) == error::not_found &&
        !found);
  check(source->bytes_read->load() == reads_before_find);
  asset_request<render_resource_ref> first, second, joined, bad, dependent;
  check(gneiss::test_support::take_value(service.load<gneiss::material>("asset://a"), first)
            .ok());
  check(
      gneiss::test_support::take_value(service.load<gneiss::material>("asset://b"), second)
          .ok());
  check(gneiss::test_support::take_value(service.reload<gneiss::material>("asset://a"),
                                         joined)
            .ok());
  check(first.resource() == joined.resource());
  first.cancel();
  check(gneiss::test_support::take_value(service.load<gneiss::material>("asset://bad"), bad)
            .ok());
  check(gneiss::test_support::take_value(
            service.load<gneiss::material>("asset://dependent"), dependent)
            .ok());
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (joined.status() == error::not_ready || second.status() == error::not_ready ||
         bad.status() == error::not_ready || dependent.status() == error::not_ready) {
    check(std::chrono::steady_clock::now() < deadline);
    (void)scheduler.run_ready();
    check(manager.async_files()->dispatch_completions(64).ok());
    manager.advance();
    std::this_thread::yield();
  }
  check(joined.status().ok() && second.status().ok() && dependent.status().ok());
  check(bad.status().ok());
  auto missing = service.load<gneiss::texture>("asset://absent");
  check(bool(missing));
  while (missing->status() == error::not_ready) {
    check(std::chrono::steady_clock::now() < deadline);
    (void)scheduler.run_ready();
    check(manager.async_files()->dispatch_completions(64).ok());
    manager.advance();
  }
  check(missing->status().failed() && missing->diagnostic().path == "asset://absent");
  check(bool(dependent.get<gneiss::material>()->textures()[0]));
  const auto bytes = source->bytes_read->load();
  check(gneiss::test_support::take_value(service.load<gneiss::material>("asset://a"), first)
            .ok());
  check(first.status().ok() && first.resource() == joined.resource());
  check(source->bytes_read->load() == bytes);
  check(gneiss::test_support::take_value(service.find<gneiss::material>("asset://a"), found)
            .ok());
  check(found == first.get<gneiss::material>());
  gneiss::ref<const gneiss::texture> wrong_type =
      dependent.get<gneiss::material>()->textures()[0];
  check(gneiss::test_support::take_value(service.find<gneiss::texture>("asset://a"),
                                         wrong_type) == error::not_found &&
        !wrong_type);
  check(gneiss::test_support::take_value(service.find<gneiss::material>("asset://missing"),
                                         found) == error::not_found &&
        !found);
  check(source->bytes_read->load() == bytes);
  const auto image = dependent.get<gneiss::material>()->textures()[0];
  const auto retained = render.share_texture(resource_handle(image));
  std::array<asset_read_record, 1> stale;
  stale[0].identity.uri = "asset://image.ktx2";
  stale[0].identity.type = static_cast<std::uint32_t>(render_resource_kind::texture);
  check(detail::asset_construction::adopt(service, std::make_shared<const texture_data>(), image,
                                          stale) == error::invalid_argument);
  check(render.share_texture(resource_handle(image)) == retained);
  found = first.get<gneiss::material>();
  auto stopping = service.reload<gneiss::material>("asset://a");
  check(bool(stopping) && stopping->status() == error::not_ready);
  auto stopping_subscriber = *stopping;
  service.request_stop();
  service.request_stop();
  check(stopping->status() == error::invalid_state &&
        stopping_subscriber.status() == error::invalid_state);
  check(first.status().ok() && first.get<gneiss::material>() == found);
  service.advance();
  check(stopping->status() == error::invalid_state);
  check(gneiss::test_support::take_value(service.find<gneiss::material>("asset://a"),
                                         found) == error::invalid_state &&
        !found);
}

} // namespace
int main() try {
  if (gneiss::initialize_meta().failed())
    return 99;
  texture_source_revisions();
  programmatic_texture_content();
  mesh_data_lifetime();
  independent_resource_identity();
  reserved_material_lifetime();
  independent_loads(gneiss::tasks::execution_mode::cooperative);
  independent_loads(gneiss::tasks::execution_mode::thread_pool);
  cpu_loading(gneiss::tasks::execution_mode::cooperative);
  cpu_loading(gneiss::tasks::execution_mode::thread_pool);
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
