// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/codec_result.hpp"
#include "../support/memory_file_fixture.hpp"
#include "../support/prepare_render.hpp"
#include "../support/value_result.hpp"
#include "engine/core/error.hpp"
#include "engine/core/tasks/task_scheduler.hpp"
#include "engine/function/render/render_service.hpp"
#include "engine/function/resource/asset_manager.hpp"
#include "engine/function/resource/detail/resource_service_impl.hpp"
#include "engine/function/resource/resource_service.hpp"
#include "engine/platform/files/file_backend.hpp"
#include "engine/platform/files/file_service.hpp"
#include "engine/resource/material/material_codec.hpp"
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
      R"({"format":"gneiss.material","version":5,"metallic":0,"roughness":1,"color":[1,1,1,1],"base_color_texture":"asset://texture"})");
  gneiss::test_support::fixture_files files;
  check(files.mount("asset://", source) == gneiss::engine::error::success);
  const std::array requests{asset_source{"asset://material", asset_type::material}};
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
    if (old_asset.kind() == asset_type::texture) {
      check(old_asset.sources.size() == 2U && new_asset.sources.size() == 2U);
      const auto& payload = new_asset.sources.back();
      asset_identity expected;
      check(files.identify("asset://payload/image.ktx2",
                           static_cast<std::uint32_t>(asset_type::texture),
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
void mesh_data_lifetime() {
  auto source = std::make_shared<memory_files>();
  const std::string text =
      R"({"format":"gneiss.mesh","version":3,"topology":"triangle_list","vertices":[[0,0,0],[1,0,0],[0,1,0]],"uvs":[[0,0],[1,0],[0,1]],"normals":[[0,0,1],[0,0,1],[0,0,1]]})";
  source->text("mesh", text);
  gneiss::test_support::fixture_files files;
  check(files.mount("asset://", source) == gneiss::engine::error::success);
  const std::array requests{asset_source{"asset://mesh", asset_type::mesh}};
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
  gneiss::engine::resource_service::implementation resources_factory{resources};
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
  gneiss::engine::resource_service::implementation resources_factory{resources};
  gneiss::engine::asset_manager asset_owner(gneiss::test_support::test_files(files));
  [[maybe_unused]] auto& cache = asset_owner.cache();
  gneiss::engine::resource_service loader{asset_owner};
  if (loader.attach_render(resources).failed())
    throw std::runtime_error("资源管理器渲染绑定失败");
  const asset_source source_a{"asset://a", asset_type::material};
  const asset_source source_b{"asset://b", asset_type::material};
  render_asset_ref a, b;
  check(gneiss::test_support::take_value(loader.reserve_asset(source_a), a) ==
        gneiss::engine::error::success);
  check(gneiss::test_support::take_value(loader.reserve_asset(source_b), b) ==
        gneiss::engine::error::success);
  gneiss::test_support::fixture_files different;
  check(different.mount("asset://", std::make_shared<memory_files>()) ==
        gneiss::engine::error::success);
  asset_manager isolated_owner(gneiss::test_support::test_files(different));
  gneiss::engine::resource_service isolated{isolated_owner};
  if (isolated.attach_render(resources).failed())
    throw std::runtime_error("资源管理器渲染绑定失败");
  render_asset_ref conflict;
  check(gneiss::test_support::take_value(isolated.reserve_asset(source_a), conflict) ==
            gneiss::engine::error::success &&
        conflict != a && gneiss::engine::asset_rid(conflict) != gneiss::engine::asset_rid(a));
  check(!isolated.owns(a));
  auto& shared = loader;
  if (shared.attach_render(resources).failed())
    throw std::runtime_error("资源管理器渲染绑定失败");
  check(gneiss::test_support::take_value(shared.reserve_asset(source_a), conflict) ==
            gneiss::engine::error::success &&
        gneiss::engine::asset_rid(conflict) == gneiss::engine::asset_rid(a));

  const auto adopt = [&](const auto& source, auto& target) {
    decoded_asset decoded;
    decoded.uri = source.uri;
    decoded.content =
        asset_ref<const material_description>{std::make_shared<const material_description>()};
    return gneiss::test_support::adopt_render_for_test(loader, std::move(decoded), target);
  };
  check(adopt(source_a, a).ok());
  check(adopt(source_b, b).ok());
  const auto retained = resources.share_material(asset_rid(a));
  decoded_asset invalid;
  invalid.uri = source_a.uri;
  check(gneiss::test_support::adopt_render_for_test(loader, invalid, a) == error::invalid_argument);
  invalid.content = asset_ref<const material_description>{};
  check(gneiss::test_support::adopt_render_for_test(loader, invalid, a) == error::invalid_argument);
  invalid.content = texture_data{};
  check(gneiss::test_support::adopt_render_for_test(loader, invalid, a) == error::invalid_argument);
  check(resources.share_material(asset_rid(a)) == retained);
}
void reserved_material_lifetime() {
  auto source = std::make_shared<memory_files>();
  gneiss::test_support::fixture_files files;
  check(files.mount("asset://", source) == gneiss::engine::error::success);
  render_service resources;
  gneiss::engine::resource_service::implementation resources_factory{resources};
  gneiss::engine::asset_manager asset_owner(gneiss::test_support::test_files(files));
  [[maybe_unused]] auto& cache = asset_owner.cache();
  gneiss::engine::resource_service loader{asset_owner};
  if (loader.attach_render(resources).failed())
    throw std::runtime_error("资源管理器渲染绑定失败");
  const asset_source request{"asset://material", asset_type::material};
  render_asset_ref target;
  check(gneiss::test_support::take_value(loader.reserve_asset(request), target) ==
        gneiss::engine::error::success);
  const auto rid = gneiss::engine::asset_rid(target);
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
  auto files =
      std::make_shared<gneiss::engine::files::service>(gneiss::engine::files::service_options{
          .max_file_bytes = resource_service::maximum_prepared_bytes});
  check(files->mount({.prefix = "asset://", .readers = {backend}, .writer = {}}).ok());
  render_service resources;
  gneiss::engine::resource_service::implementation resources_factory{resources};
  asset_manager manager(gneiss::test_support::test_files(files));
  gneiss::engine::resource_service loader{manager};
  if (loader.attach_render(resources).failed())
    throw std::runtime_error("资源管理器渲染绑定失败");
  check(loader.configure_loading(scheduler).ok());
  memory_files fixture;
  fixture.pixel(std::byte{42});
  fixture.text(
      "texture",
      R"({"format":"gneiss.texture","version":2,"source":"asset://image.ktx2","color_space":"srgb","usage":"default"})");
  resource_request request;
  auto advance = [&] {
    (void)scheduler.run_ready();
    check(files->dispatch_completions(64).ok());
    manager.advance();
    loader.advance();
    std::this_thread::yield();
  };
  auto until = [&](auto predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!predicate()) {
      check(std::chrono::steady_clock::now() < deadline);
      advance();
    }
  };
  check(gneiss::test_support::take_value(loader.load<texture_resource>("asset://texture"), request)
            .ok());
  auto target = request.resource();
  resource_request duplicate;
  check(
      gneiss::test_support::take_value(loader.load<texture_resource>("asset://texture"), duplicate)
          .ok());
  check(duplicate.resource() == target);
  until([&] { return bool(backend->callback); });
  check(resources.texture_status(asset_rid(target)) == error::not_ready);
  request.cancel(); // 另一个调用方仍在等待，不取消共享工作。
  backend->complete(fixture.files.at("texture"));
  until([&] { return bool(backend->callback); });
  check(backend->path == "image.ktx2");
  backend->complete(fixture.files.at("image.ktx2"));
  until([&] { return duplicate.status() != error::not_ready; });
  check(duplicate.status().ok());
  const auto previous = resources.share_texture(asset_rid(target));
  check(previous && previous->levels.front().pixels.front() == std::byte{42});
  const auto reads = backend->reads;
  check(gneiss::test_support::take_value(loader.load<texture_resource>("asset://texture"), request)
            .ok());
  check(request.status().ok() && backend->reads == reads);
  check(gneiss::test_support::take_value(loader.load<texture_resource>("asset://texture", true),
                                         request)
            .ok());
  until([&] { return bool(backend->callback); });
  backend->complete({}, error::io);
  until([&] { return request.status() != error::not_ready; });
  check(request.status() == error::io && resources.share_texture(asset_rid(target)) == previous);
  check(gneiss::test_support::take_value(loader.load<texture_resource>("asset://texture", true),
                                         request)
            .ok());
  until([&] { return bool(backend->callback); });
  request.cancel();
  advance();
  backend->complete(fixture.files.at("texture"));
  advance();
  check(resources.share_texture(asset_rid(target)) == previous);
  fixture.text(
      "mesh",
      R"({"format":"gneiss.mesh","version":3,"topology":"triangle_list","vertices":[[0,0,0],[1,0,0],[0,1,0]],"uvs":[[0,0],[0,0],[0,0]],"normals":[[0.0,0.0,1.0],[0.0,0.0,1.0],[0.0,0.0,1.0]]})");
  fixture.text(
      "material",
      R"({"format":"gneiss.material","version":5,"metallic":0,"roughness":1,"color":[1,1,1,1],"base_color_texture":"asset://texture"})");
  check(
      gneiss::test_support::take_value(loader.load<material_resource>("asset://material"), request)
          .ok());
  until([&] {
    if (backend->callback)
      backend->complete(fixture.files.at(backend->path));
    return request.status() != error::not_ready;
  });
  check(request.status().ok());
  check(request.get<material_resource>()->textures()[0] == asset_cast<texture_resource>(target));
  check(gneiss::test_support::take_value(loader.load({"asset://mesh", asset_type::mesh}, true, 1),
                                         request)
            .ok());
  until([&] {
    if (backend->callback)
      backend->complete(fixture.files.at(backend->path));
    return request.status() != error::not_ready;
  });
  check(request.status().failed());
  check(gneiss::test_support::take_value(loader.load<texture_resource>("asset://texture", true),
                                         request)
            .ok());
  loader.request_stop();
  check(loader.stopped() && request.status() == error::invalid_state);
  check(!manager.stopped());
  if (backend->callback)
    backend->complete({});
  advance();
  texture rejected;
  check(gneiss::test_support::take_value(loader.reserve_texture(), rejected) ==
            error::invalid_state &&
        !rejected);
  check(loader.configure_loading(scheduler) == error::invalid_state);
  check(gneiss::test_support::take_value(loader.load<texture_resource>("asset://texture"),
                                         request) == error::invalid_state);
  // 资源服务停止后仍能直接加载 CPU 内容，结果移动领取且不写入长期缓存。
  manager.collect_unused();
  const auto cached = manager.cache().size();
  asset_data_request<mesh_data> mesh_request;
  check(gneiss::test_support::assign_value(manager.load_mesh_data("asset://mesh"), mesh_request)
            .ok());
  mesh_data mesh_content;
  asset_data_metrics metrics;
  error loaded{error::not_ready};
  until([&] {
    if (backend->callback)
      backend->complete(fixture.files.at(backend->path));
    auto received = mesh_request.take();
    if (!received) {
      loaded = received.error().error;
      return loaded != error::not_ready;
    }
    mesh_content = std::move(received->data);
    metrics = std::move(received->metrics);
    loaded = error::success;
    return true;
  });
  check(loaded.ok() && mesh_content.vertices.size() == 3U && !mesh_request);
  check(metrics.sources.size() == 1U && metrics.bytes == mesh_content.data_bytes());
  check(gneiss::test_support::take_request(mesh_request, mesh_content) == error::invalid_state);
  check(manager.cache().size() == cached);
  asset_ref<const mesh_data> absent_mesh;
  check(gneiss::test_support::assign_value(manager.find_loaded<mesh_data>("asset://mesh"),
                                           absent_mesh) == error::not_found);

  asset_data_request<texture_data> texture_request;
  check(gneiss::test_support::assign_value(manager.load_texture_data("asset://texture", {}),
                                           texture_request)
            .ok());
  texture_data texture_content;
  until([&] {
    if (backend->callback)
      backend->complete(fixture.files.at(backend->path));
    loaded =
        gneiss::test_support::take_request(texture_request, texture_content, nullptr, &metrics);
    return loaded != error::not_ready;
  });
  check(loaded.ok() && texture_content.levels.front().pixels.front() == std::byte{42});
  check(metrics.sources.size() == 2U &&
        metrics.sources.back().identity.uri == "asset://image.ktx2");
  check(manager.cache().size() == cached);

  check(gneiss::test_support::assign_value(manager.load_texture_data("asset://texture", {}),
                                           texture_request)
            .ok());
  until([&] { return bool(backend->callback); });
  manager.request_stop();
  check(gneiss::test_support::take_request(texture_request, texture_content) ==
        error::invalid_state);
  backend->complete(fixture.files.at("texture"));
  until([&] { return manager.stopped(); });
  check(gneiss::test_support::assign_value(manager.load_mesh_data("asset://mesh"), mesh_request) ==
        error::invalid_state);
}
void independent_loads(execution_mode mode) {
  task_scheduler scheduler({.mode = mode});
  auto source = std::make_shared<memory_files>();
  source->text(
      "a",
      R"({"format":"gneiss.material","version":5,"metallic":0,"roughness":1,"color":[1,1,1,1],"base_color_texture":null})");
  source->text(
      "b",
      R"({"format":"gneiss.material","version":5,"metallic":0,"roughness":1,"color":[0,1,0,1],"base_color_texture":null})");
  source->text(
      "bad",
      R"({"format":"gneiss.material","version":5,"metallic":0,"roughness":1,"color":[1,1,1,1],"base_color_texture":"asset://absent"})");
  source->text(
      "dependent",
      R"({"format":"gneiss.material","version":5,"metallic":0,"roughness":1,"color":[1,1,1,1],"base_color_texture":"asset://texture"})");
  source->text(
      "texture",
      R"({"format":"gneiss.texture","version":2,"source":"asset://image.ktx2","color_space":"srgb","usage":"default"})");
  source->pixel(std::byte{42});
  test_support::fixture_files files;
  check(files.mount("asset://", source).ok());
  asset_manager manager(test_support::test_files(files));
  render_service render;
  resource_service service(manager);
  check(service.attach_render(render).ok());
  check(service.configure_loading(scheduler).ok());
  material found;
  const auto reads_before_find = source->bytes_read->load();
  check(gneiss::test_support::take_value(service.find<material_resource>("asset://a"), found) ==
            error::not_found &&
        !found);
  check(source->bytes_read->load() == reads_before_find);
  resource_request first, second, joined, bad, dependent;
  check(gneiss::test_support::take_value(service.load<material_resource>("asset://a"), first).ok());
  check(
      gneiss::test_support::take_value(service.load<material_resource>("asset://b"), second).ok());
  check(gneiss::test_support::take_value(service.load<material_resource>("asset://a", true), joined)
            .ok());
  check(first.resource() == joined.resource());
  first.cancel();
  check(gneiss::test_support::take_value(service.load<material_resource>("asset://bad"), bad).ok());
  check(gneiss::test_support::take_value(service.load<material_resource>("asset://dependent"),
                                         dependent)
            .ok());
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (joined.status() == error::not_ready || second.status() == error::not_ready ||
         bad.status() == error::not_ready || dependent.status() == error::not_ready) {
    check(std::chrono::steady_clock::now() < deadline);
    (void)scheduler.run_ready();
    check(manager.async_files()->dispatch_completions(64).ok());
    manager.advance();
    service.advance();
    std::this_thread::yield();
  }
  check(joined.status().ok() && second.status().ok() && dependent.status().ok());
  check(bad.status().failed() && bad.diagnostic().path == "asset://absent");
  check(bool(dependent.get<material_resource>()->textures()[0]));
  const auto bytes = source->bytes_read->load();
  check(gneiss::test_support::take_value(service.load<material_resource>("asset://a"), first).ok());
  check(first.status().ok() && first.resource() == joined.resource());
  check(source->bytes_read->load() == bytes);
  check(gneiss::test_support::take_value(service.find<material_resource>("asset://a"), found).ok());
  check(found == first.get<material_resource>());
  texture wrong_type = dependent.get<material_resource>()->textures()[0];
  check(gneiss::test_support::take_value(service.find<texture_resource>("asset://a"), wrong_type) ==
            error::not_found &&
        !wrong_type);
  check(gneiss::test_support::take_value(service.find<material_resource>("asset://missing"),
                                         found) == error::not_found &&
        !found);
  check(source->bytes_read->load() == bytes);
  const auto image = dependent.get<material_resource>()->textures()[0];
  const auto retained = render.share_texture(resource_handle(image));
  std::array<asset_data_source, 1> stale;
  stale[0].identity.uri = "asset://image.ktx2";
  stale[0].identity.type = static_cast<std::uint32_t>(asset_type::texture);
  check(resource_service::implementation::adopt(service, texture_data{}, image, stale) ==
        error::invalid_argument);
  check(render.share_texture(resource_handle(image)) == retained);
  found = first.get<material_resource>();
  service.request_stop();
  check(gneiss::test_support::take_value(service.find<material_resource>("asset://a"), found) ==
            error::invalid_state &&
        !found);
}

} // namespace
int main() try {
  prepared_source_revisions();
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
