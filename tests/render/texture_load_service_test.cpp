// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/codec_result.hpp"
#include "../support/memory_file_fixture.hpp"
#include "../support/prepare_render.hpp"
#include "../support/value_result.hpp"
#include "engine/core/error.hpp"
#include "engine/core/serialization/json.hpp"
#include "engine/core/tasks/task_scheduler.hpp"
#include "engine/function/meta_registration.hpp"
#include "engine/function/render/render_service.hpp"
#include "engine/function/asset/asset_manager.hpp"
#include "engine/function/asset/detail/asset_construction.hpp"
#include "engine/platform/files/file_backend.hpp"
#include "engine/platform/files/file_service.hpp"
#include "engine/resource/material/material_description.hpp"
#include "engine/resource/texture/texture_ktx2.hpp"
#include <algorithm>
#include <stdexcept>

#include <atomic>
#include <cstdio>
#include <map>
#include <source_location>

namespace {
using namespace gneiss;
using namespace gneiss::engine;
void check(bool value, std::source_location at = std::source_location::current()) {
  if (!value) {
    throw std::runtime_error("异步资产契约失败，行=" + std::to_string(at.line()));
  }
}
struct memory_files final : gneiss::test_support::memory_file_fixture {
  std::map<std::string, std::vector<std::byte>> files;
  mutable std::thread::id reader;
  std::shared_ptr<std::atomic_size_t> bytes_read = std::make_shared<std::atomic_size_t>();
  gneiss::engine::error read(std::string_view path,
                             std::vector<std::byte>& bytes) const noexcept override {
    return read_bounded(path, SIZE_MAX, bytes);
  }
  gneiss::engine::error read_bounded(std::string_view path, std::size_t limit,
                                     std::vector<std::byte>& bytes) const noexcept override {
    reader = std::this_thread::get_id();
    const auto found = files.find(std::string(path));
    if (found == files.end()) {
      return gneiss::engine::error::not_found;
    }
    if (found->second.size() > limit) {
      return gneiss::engine::error::invalid_argument;
    }
    bytes = found->second;
    bytes_read->fetch_add(bytes.size(), std::memory_order_relaxed);
    return gneiss::engine::error::success;
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
class delayed_backend final : public gneiss::engine::files::backend {
public:
  std::string path;
  gneiss::engine::files::callback<gneiss::engine::files::bytes> callback;
  std::size_t reads{};
  error read(std::string_view requested, gneiss::engine::files::read_options,
             gneiss::engine::files::callback<gneiss::engine::files::bytes> completed) override {
    check(!callback);
    path = requested;
    callback = std::move(completed);
    ++reads;
    return error::success;
  }
  void complete(gneiss::engine::files::bytes bytes, error status = error::success) {
    auto completed = std::move(callback);
    check(static_cast<bool>(completed));
    completed({.status = status, .data = std::move(bytes)});
  }
};
void prepared_source_revisions() {
  auto source = std::make_shared<memory_files>();
  source->pixel(std::byte{10});
  source->files["payload/image.ktx2"] = source->files.at("image.ktx2");
  source->text(
      "texture",
      R"({"format":"gneiss.texture","version":2,"source":"asset://payload/image.ktx2","color_space":"srgb","usage":"default"})");
  source->text(
      "material",
      R"({"format":"gneiss.material","version":6,"parameters":{"metallic":0,"roughness":1,"color":[1,1,1,1]},"texture_uris":["asset://texture","","","",""]})");
  gneiss::test_support::fixture_files files;
  check(files.mount("asset://", source) == gneiss::engine::error::success);
  const std::array requests{
      render_resource_source{"asset://material", render_resource_kind::material}};
  asset_manager owner(gneiss::test_support::test_files(files));
  prepared_batch before;
  prepared_batch after;
  asset_diagnostic diagnostic;
  check(gneiss::test_support::prepare_render_for_test(owner, files, requests, before, diagnostic,
                                                      {}) == gneiss::engine::error::success);
  prepared_batch repeated;
  check(gneiss::test_support::prepare_render_for_test(owner, files, requests, repeated, diagnostic,
                                                      {}) == gneiss::engine::error::success);
  source->pixel(std::byte{20});
  source->files["payload/image.ktx2"] = source->files.at("image.ktx2");
  check(gneiss::test_support::prepare_render_for_test(owner, files, requests, after, diagnostic,
                                                      {}) == gneiss::engine::error::success);
  check(before.assets.size() == 2U && after.assets.size() == 2U);
  for (std::size_t index = 0; index < before.assets.size(); ++index) {
    const auto& old_asset = before.assets[index];
    const auto& new_asset = after.assets[index];
    check(old_asset.uri == new_asset.uri);
    if (!old_asset.sources.empty()) {
      check(old_asset.sources.front().identity == new_asset.sources.front().identity);
      check(old_asset.sources.front().digest == new_asset.sources.front().digest);
    }
    if (old_asset.kind() == render_resource_kind::texture) {
      check(old_asset.sources.size() == 2U && new_asset.sources.size() == 2U);
      const auto& payload = new_asset.sources.back();
      asset_identity expected;
      check(files.identify("asset://payload/image.ktx2",
                           static_cast<std::uint32_t>(render_resource_kind::texture),
                           expected) == gneiss::engine::error::success);
      check(payload.identity == expected && payload.identity == old_asset.sources.back().identity);
      check(payload.bytes == source->files.at("image.ktx2").size());
      check(payload.digest == gneiss::engine::sha256(source->files.at("image.ktx2")));
      check(payload.digest != old_asset.sources.back().digest);
    } else {
      check(old_asset.sources.empty() && new_asset.sources.empty());
      check(std::get<asset_ref<const material_description>>(new_asset.content)->texture_uris[0] ==
            "asset://texture");
    }
  }
}
void programmatic_texture_content() {
  render_service render;
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
  created = error::invalid_state;
  check(factory.drain() == 1);
}

void mesh_data_lifetime() {
  auto source = std::make_shared<memory_files>();
  const std::string text =
      R"({"format":"gneiss.mesh","version":3,"topology":"triangle_list","vertices":[[0,0,0],[1,0,0],[0,1,0]],"uvs":[[0,0],[1,0],[0,1]],"normals":[[0,0,1],[0,0,1],[0,0,1]]})";
  source->text("mesh", text);
  gneiss::test_support::fixture_files files;
  check(files.mount("asset://", source) == gneiss::engine::error::success);
  const std::array requests{render_resource_source{"asset://mesh", render_resource_kind::mesh}};
  auto prepare = [&](const gneiss::test_support::fixture_files& vfs) {
    prepared_batch batch;
    asset_diagnostic diagnostic;
    asset_manager owner(gneiss::test_support::test_files(vfs));
    check(gneiss::test_support::prepare_render_for_test(owner, vfs, requests, batch, diagnostic,
                                                        {}) == gneiss::engine::error::success);
    return batch;
  };
  auto first = prepare(files);
  const auto* vertices = std::get<mesh_data>(first.assets.front().content).vertices.data();
  auto mesh = std::make_shared<const mesh_data>(
      std::move(std::get<mesh_data>(first.assets.front().content)));
  check(mesh->vertices.data() == vertices);
  render_service resources;
  gneiss::engine::detail::asset_construction resources_factory{resources};
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
  gneiss::engine::rid rid{};
  check(gneiss::test_support::assign_value(resources.create_prepared_mesh(mesh), rid) ==
        gneiss::engine::error::success);
  check(resources.share_mesh(rid) == mesh);
  auto frame = resources.share_mesh(rid);
  check(resources.destroy_mesh(rid) == gneiss::engine::error::success &&
        !resources.share_mesh(rid));
  std::weak_ptr<const mesh_data> lifetime = mesh;
  first = {};
  mesh.reset();
  check(!lifetime.expired() && frame->vertices[1].position.x == 1.0F);
  frame.reset();
  check(lifetime.expired());
}
void independent_resource_identity() {
  gneiss::test_support::fixture_files files;
  check(files.mount("asset://", std::make_shared<memory_files>()) ==
        gneiss::engine::error::success);
  render_service resources;
  gneiss::engine::detail::asset_construction resources_factory{resources};
  gneiss::engine::asset_manager asset_owner(gneiss::test_support::test_files(files));
  [[maybe_unused]] auto& cache = asset_owner;
  auto& loader = asset_owner;
  if (loader.attach_render(resources).failed())
    throw std::runtime_error("资源管理器渲染绑定失败");
  const render_resource_source source_a{"asset://a", render_resource_kind::material};
  const render_resource_source source_b{"asset://b", render_resource_kind::material};
  render_resource_ref a, b;
  check(gneiss::test_support::take_value(loader.reserve_resource(source_a), a) ==
        gneiss::engine::error::success);
  check(gneiss::test_support::take_value(loader.reserve_resource(source_b), b) ==
        gneiss::engine::error::success);
  gneiss::test_support::fixture_files different;
  check(different.mount("asset://", std::make_shared<memory_files>()) ==
        gneiss::engine::error::success);
  asset_manager isolated_owner(gneiss::test_support::test_files(different));
  auto& isolated = isolated_owner;
  if (isolated.attach_render(resources).failed())
    throw std::runtime_error("资源管理器渲染绑定失败");
  render_resource_ref conflict;
  check(gneiss::test_support::take_value(isolated.reserve_resource(source_a), conflict) ==
            gneiss::engine::error::success &&
        conflict != a && gneiss::engine::resource_rid(conflict) != gneiss::engine::resource_rid(a));
  check(!isolated.owns(a));
  auto& shared = loader;
  if (shared.attach_render(resources).failed())
    throw std::runtime_error("资源管理器渲染绑定失败");
  check(gneiss::test_support::take_value(shared.reserve_resource(source_a), conflict) ==
            gneiss::engine::error::success &&
        gneiss::engine::resource_rid(conflict) == gneiss::engine::resource_rid(a));

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
  check(files.mount("asset://", source) == gneiss::engine::error::success);
  render_service resources;
  gneiss::engine::detail::asset_construction resources_factory{resources};
  gneiss::engine::asset_manager asset_owner(gneiss::test_support::test_files(files));
  [[maybe_unused]] auto& cache = asset_owner;
  auto& loader = asset_owner;
  if (loader.attach_render(resources).failed())
    throw std::runtime_error("资源管理器渲染绑定失败");
  const render_resource_source request{"asset://material", render_resource_kind::material};
  render_resource_ref target;
  check(gneiss::test_support::take_value(loader.reserve_resource(request), target) ==
        gneiss::engine::error::success);
  const auto rid = gneiss::engine::resource_rid(target);
  decoded_asset prepared;
  prepared.uri = request.uri;
  prepared.content =
      asset_ref<const material_description>{std::make_shared<const material_description>()};
  check(resources.material_status(rid) == error::not_ready);
  check(gneiss::test_support::adopt_render_for_test(loader, std::move(prepared), target).ok());
  check(resources.material_status(rid) == gneiss::engine::error::success);
  target = {};
  loader.collect_unused();
  check(resources.material_status(rid) == gneiss::engine::error::invalid_handle);
}
void cpu_loading(execution_mode mode) {
  task_scheduler scheduler({.mode = mode});
  auto backend = std::make_shared<delayed_backend>();
  auto files = std::make_shared<gneiss::engine::files::service>(
      gneiss::engine::files::service_options{.max_file_bytes = 64U * 1024U * 1024U});
  check(files->mount({.prefix = "asset://", .readers = {backend}, .writer = {}}).ok());
  render_service resources;
  gneiss::engine::detail::asset_construction resources_factory{resources};
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
  check(gneiss::test_support::take_value(loader.load<gneiss::engine::texture>("asset://texture"),
                                         request)
            .ok());
  auto target = request.resource();
  asset_request<render_resource_ref> duplicate;
  check(gneiss::test_support::take_value(loader.load<gneiss::engine::texture>("asset://texture"),
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
  auto typed_request = manager.load<gneiss::engine::texture>("asset://texture");
  check(bool(typed_request));
  auto typed = typed_request->take();
  check(bool(typed) && *typed == resource_cast<gneiss::engine::texture>(target));
  auto found_typed = manager.find_loaded<gneiss::engine::texture>("asset://texture");
  check(bool(found_typed) && *found_typed == *typed);
  asset_load_options mismatched;
  mismatched.texture_profile.sampled_transfer_formats[0] = true;
  check(manager.find_loaded<gneiss::engine::texture>("asset://texture", mismatched).error() ==
        error::not_found);
  check(manager.load<gneiss::engine::texture>("asset://texture", mismatched).error() ==
        error::invalid_argument);
  // 默认装配负载不在最终资产索引中。
  check(manager.find_loaded<texture_data>("asset://texture").error() == error::unsupported);
  const auto previous = resources.share_texture(resource_rid(target));
  check(previous && previous->levels.front().pixels.front() == std::byte{42});
  const auto reads = backend->reads;
  check(gneiss::test_support::take_value(loader.load<gneiss::engine::texture>("asset://texture"),
                                         request)
            .ok());
  check(request.status().ok() && backend->reads == reads);
  check(gneiss::test_support::take_value(
            loader.load<gneiss::engine::texture>("asset://texture", true), request)
            .ok());
  until([&] { return bool(backend->callback); });
  backend->complete({}, error::io);
  until([&] { return request.status() != error::not_ready; });
  check(request.status() == error::io && resources.share_texture(resource_rid(target)) == previous);
  check(gneiss::test_support::take_value(
            loader.load<gneiss::engine::texture>("asset://texture", true), request)
            .ok());
  until([&] { return bool(backend->callback); });
  request.cancel();
  advance();
  backend->complete(fixture.files.at("texture"));
  advance();
  check(resources.share_texture(resource_rid(target)) == previous);
  fixture.text(
      "mesh",
      R"({"format":"gneiss.mesh","version":3,"topology":"triangle_list","vertices":[[0,0,0],[1,0,0],[0,1,0]],"uvs":[[0,0],[0,0],[0,0]],"normals":[[0.0,0.0,1.0],[0.0,0.0,1.0],[0.0,0.0,1.0]]})");
  fixture.text(
      "material",
      R"({"format":"gneiss.material","version":6,"parameters":{"metallic":0,"roughness":1,"color":[1,1,1,1]},"texture_uris":["asset://texture","","","",""]})");
  check(gneiss::test_support::take_value(loader.load<gneiss::engine::material>("asset://material"),
                                         request)
            .ok());
  until([&] {
    if (backend->callback)
      backend->complete(fixture.files.at(backend->path));
    return request.status() != error::not_ready;
  });
  check(request.status().ok());
  check(request.get<gneiss::engine::material>()->textures()[0] ==
        resource_cast<gneiss::engine::texture>(target));
  check(gneiss::test_support::take_value(
            loader.load({"asset://mesh", render_resource_kind::mesh}, true), request)
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
  check(manager.find_loaded<mesh_data>("asset://mesh").error() == error::unsupported);
  check(manager.find_loaded<material_description>("asset://material").error() ==
        error::unsupported);
  check(manager.store<mesh_data>("asset://mesh", std::make_shared<const mesh_data>()).error() ==
        error::unsupported);
  check(manager.store<texture_data>("asset://texture", std::make_shared<const texture_data>())
            .error() == error::unsupported);
  check(manager
            .store<material_description>("asset://material",
                                         std::make_shared<const material_description>())
            .error() == error::unsupported);
  check(backend->reads == reads_before_rejection && manager.cached_count() == cached);

  auto mesh_request = manager.load<gneiss::engine::mesh>("asset://mesh", true);
  auto cancelled = manager.load<gneiss::engine::mesh>("asset://mesh");
  check(mesh_request && cancelled);
  cancelled->cancel();
  auto joined = manager.load<gneiss::engine::mesh>("asset://mesh");
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
  auto texture_request = manager.load<gneiss::engine::texture>("asset://cpu_texture");
  check(bool(texture_request));
  until([&] { return bool(backend->callback); });
  manager.request_stop();
  check(texture_request->status() == error::invalid_state);
  backend->complete(fixture.files.at("cpu_texture"));
  until([&] { return manager.stopped(); });
  gneiss::engine::ref<const gneiss::engine::texture> rejected;
  check(gneiss::test_support::take_value(manager.create_texture(texture_view{}), rejected) ==
            error::invalid_state &&
        !rejected);
  check(manager.configure(scheduler) == error::invalid_state);
  check(manager.attach_render(resources) == error::invalid_state);
  check(manager.load<gneiss::engine::texture>("asset://texture").error() == error::invalid_state);
  check(manager.load<gneiss::engine::mesh>("asset://mesh").error() == error::invalid_state);
}
void independent_loads(execution_mode mode) {
  task_scheduler scheduler({.mode = mode});
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
  render_service render;
  auto& service = manager;
  // 配置与 Render 绑定顺序不影响加载，且两者都不能在停止后重新装配。
  if (mode == execution_mode::cooperative) {
    check(service.configure(scheduler).ok());
    check(service.attach_render(render).ok());
  } else {
    check(service.attach_render(render).ok());
    check(service.configure(scheduler).ok());
  }
  check(service.configure(scheduler).ok());
  gneiss::engine::ref<const gneiss::engine::material> found;
  const auto reads_before_find = source->bytes_read->load();
  check(gneiss::test_support::take_value(service.find_loaded<gneiss::engine::material>("asset://a"),
                                         found) == error::not_found &&
        !found);
  check(source->bytes_read->load() == reads_before_find);
  asset_request<render_resource_ref> first, second, joined, bad, dependent;
  check(gneiss::test_support::take_value(service.load<gneiss::engine::material>("asset://a"), first)
            .ok());
  check(
      gneiss::test_support::take_value(service.load<gneiss::engine::material>("asset://b"), second)
          .ok());
  check(gneiss::test_support::take_value(service.load<gneiss::engine::material>("asset://a", true),
                                         joined)
            .ok());
  check(first.resource() == joined.resource());
  first.cancel();
  check(gneiss::test_support::take_value(service.load<gneiss::engine::material>("asset://bad"), bad)
            .ok());
  check(gneiss::test_support::take_value(
            service.load<gneiss::engine::material>("asset://dependent"), dependent)
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
  check(bad.status().failed() && bad.diagnostic().path == "asset://absent");
  check(bool(dependent.get<gneiss::engine::material>()->textures()[0]));
  const auto bytes = source->bytes_read->load();
  check(gneiss::test_support::take_value(service.load<gneiss::engine::material>("asset://a"), first)
            .ok());
  check(first.status().ok() && first.resource() == joined.resource());
  check(source->bytes_read->load() == bytes);
  check(gneiss::test_support::take_value(service.find_loaded<gneiss::engine::material>("asset://a"),
                                         found)
            .ok());
  check(found == first.get<gneiss::engine::material>());
  gneiss::engine::ref<const gneiss::engine::texture> wrong_type =
      dependent.get<gneiss::engine::material>()->textures()[0];
  check(gneiss::test_support::take_value(service.find_loaded<gneiss::engine::texture>("asset://a"),
                                         wrong_type) == error::not_found &&
        !wrong_type);
  check(gneiss::test_support::take_value(
            service.find_loaded<gneiss::engine::material>("asset://missing"), found) ==
            error::not_found &&
        !found);
  check(source->bytes_read->load() == bytes);
  const auto image = dependent.get<gneiss::engine::material>()->textures()[0];
  const auto retained = render.share_texture(resource_handle(image));
  std::array<asset_read_record, 1> stale;
  stale[0].identity.uri = "asset://image.ktx2";
  stale[0].identity.type = static_cast<std::uint32_t>(render_resource_kind::texture);
  check(detail::asset_construction::adopt(service, std::make_shared<const texture_data>(), image,
                                          stale) == error::invalid_argument);
  check(render.share_texture(resource_handle(image)) == retained);
  found = first.get<gneiss::engine::material>();
  auto stopping = service.load<gneiss::engine::material>("asset://a", true);
  check(bool(stopping) && stopping->status() == error::not_ready);
  auto stopping_subscriber = *stopping;
  service.request_stop();
  service.request_stop();
  check(stopping->status() == error::invalid_state &&
        stopping_subscriber.status() == error::invalid_state);
  check(first.status().ok() && first.get<gneiss::engine::material>() == found);
  service.advance();
  check(stopping->status() == error::invalid_state);
  check(gneiss::test_support::take_value(service.find_loaded<gneiss::engine::material>("asset://a"),
                                         found) == error::invalid_state &&
        !found);
}

} // namespace
int main() try {
  if (gneiss::engine::initialize_meta().failed())
    return 99;
  prepared_source_revisions();
  programmatic_texture_content();
  mesh_data_lifetime();
  independent_resource_identity();
  reserved_material_lifetime();
  independent_loads(execution_mode::cooperative);
  independent_loads(execution_mode::thread_pool);
  cpu_loading(execution_mode::cooperative);
  cpu_loading(execution_mode::thread_pool);
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
