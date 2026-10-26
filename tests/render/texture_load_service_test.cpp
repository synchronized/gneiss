// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/memory_file_fixture.hpp"
#include "../support/prepare_render.hpp"
#include "engine/core/result.hpp"
#include "engine/function/render/render_resource_service.hpp"
#include "engine/function/render/texture_load_service.hpp"
#include "engine/platform/files/file_backend.hpp"
#include "engine/platform/files/file_service.hpp"
#include "engine/resource/asset_manager.hpp"
#include "engine/resource/loaders/material_serialization.hpp"
#include "engine/resource/loaders/texture_ktx2.hpp"
#include <algorithm>
#include <granit/asset_tools/texture_builder.hpp>

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
  gneiss::engine::result read(std::string_view path,
                              std::vector<std::byte>& bytes) const noexcept override {
    return read_bounded(path, SIZE_MAX, bytes);
  }
  gneiss::engine::result read_bounded(std::string_view path, std::size_t limit,
                                      std::vector<std::byte>& bytes) const noexcept override {
    reader = std::this_thread::get_id();
    const auto found = files.find(std::string(path));
    if (found == files.end()) {
      return gneiss::engine::result::not_found;
    }
    if (found->second.size() > limit) {
      return gneiss::engine::result::invalid_argument;
    }
    bytes = found->second;
    bytes_read->fetch_add(bytes.size(), std::memory_order_relaxed);
    return gneiss::engine::result::success;
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
    check(encode_texture_ktx2(
              {.transfer = texture_transfer::srgb,
               .levels = {{.width = 1U,
                           .height = 1U,
                           .pixels = {red, std::byte{}, std::byte{}, std::byte{255}}}}},
              files["image.ktx2"], diagnostic) == texture_ktx2_result::success);
  }
};
// 人工延迟后端只接受回调；用于验证文件等待与 CPU 调度完全分离。
class delayed_backend final : public file_backend {
public:
  std::string path;
  file_callback<file_bytes> callback;
  std::size_t reads{};
  result read(std::string_view requested, read_options,
              file_callback<file_bytes> completed) override {
    check(!callback);
    path = requested;
    callback = std::move(completed);
    ++reads;
    return result::success;
  }
  void complete(file_bytes bytes, result status = result::success) {
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
      R"({"format":"gneiss.texture","version":1,"source":"asset://payload/image.ktx2","color_space":"srgb"})");
  source->text(
      "material",
      R"({"format":"gneiss.material","version":5,"metallic":0,"roughness":1,"color":[1,1,1,1],"base_color_texture":"asset://texture"})");
  gneiss::test_support::fixture_files files;
  check(files.mount("asset://", source) == gneiss::engine::result::success);
  const std::array requests{asset_preparation_source{"asset://material", asset_type::material}};
  asset_manager owner(gneiss::test_support::test_files(files));
  prepared_batch before;
  prepared_batch after;
  asset_diagnostic diagnostic;
  check(gneiss::test_support::prepare_render_for_test(owner, files, requests, before, diagnostic,
                                                      {}) == gneiss::engine::result::success);
  prepared_batch repeated;
  check(gneiss::test_support::prepare_render_for_test(owner, files, requests, repeated, diagnostic,
                                                      {}) == gneiss::engine::result::success);
  source->pixel(std::byte{20});
  source->files["payload/image.ktx2"] = source->files.at("image.ktx2");
  check(gneiss::test_support::prepare_render_for_test(owner, files, requests, after, diagnostic,
                                                      {}) == gneiss::engine::result::success);
  check(before.assets.size() == 2U && after.assets.size() == 2U);
  for (std::size_t index = 0; index < before.assets.size(); ++index) {
    const auto& old_asset = before.assets[index];
    const auto& new_asset = after.assets[index];
    check(old_asset.source.uri == new_asset.source.uri);
    if (!old_asset.sources.empty()) {
      check(old_asset.sources.front().identity == new_asset.sources.front().identity);
      check(old_asset.sources.front().digest == new_asset.sources.front().digest);
    }
    if (old_asset.source.type == asset_type::texture) {
      check(old_asset.sources.size() == 2U && new_asset.sources.size() == 2U);
      const auto& payload = new_asset.sources.back();
      asset_identity expected;
      check(files.identify("asset://payload/image.ktx2",
                           static_cast<std::uint32_t>(asset_type::texture),
                           expected) == gneiss::engine::result::success);
      check(payload.identity == expected && payload.identity == old_asset.sources.back().identity);
      check(payload.bytes == source->files.at("image.ktx2").size());
      check(payload.digest == gneiss::engine::sha256(source->files.at("image.ktx2")));
      check(payload.digest != old_asset.sources.back().digest);
    } else {
      check(old_asset.sources.empty() && new_asset.sources.empty());
      check(new_asset.texture_uris[0] == "asset://texture");
    }
  }
}
void mesh_data_lifetime() {
  auto source = std::make_shared<memory_files>();
  const std::string text =
      R"({"format":"gneiss.mesh","version":3,"topology":"triangle_list","vertices":[[0,0,0],[1,0,0],[0,1,0]],"uvs":[[0,0],[1,0],[0,1]],"normals":[[0,0,1],[0,0,1],[0,0,1]]})";
  source->text("mesh", text);
  gneiss::test_support::fixture_files files;
  check(files.mount("asset://", source) == gneiss::engine::result::success);
  const std::array requests{asset_preparation_source{"asset://mesh", asset_type::mesh}};
  auto prepare = [&](const gneiss::test_support::fixture_files& vfs) {
    prepared_batch batch;
    asset_diagnostic diagnostic;
    asset_manager owner(gneiss::test_support::test_files(vfs));
    check(gneiss::test_support::prepare_render_for_test(owner, vfs, requests, batch, diagnostic,
                                                        {}) == gneiss::engine::result::success);
    return batch;
  };
  auto first = prepare(files);
  const auto* vertices = first.assets.front().mesh.vertices.data();
  auto mesh = std::make_shared<const mesh_data>(std::move(first.assets.front().mesh));
  check(mesh->vertices.data() == vertices);
  render_resource_service resources;
  gneiss::engine::rid rid{};
  check(resources.create_prepared_mesh(mesh, &rid) == gneiss::engine::result::success);
  check(resources.share_mesh(rid) == mesh);
  auto frame = resources.share_mesh(rid);
  check(resources.destroy_mesh(rid) == gneiss::engine::result::success &&
        !resources.share_mesh(rid));
  std::weak_ptr<const mesh_data> lifetime = mesh;
  first = {};
  mesh.reset();
  check(!lifetime.expired() && frame->vertices[1].x == 1.0F);
  frame.reset();
  check(lifetime.expired());
}
void independent_update_sequences() {
  gneiss::test_support::fixture_files files;
  check(files.mount("asset://", std::make_shared<memory_files>()) ==
        gneiss::engine::result::success);
  render_resource_service resources;
  gneiss::engine::asset_manager asset_owner(gneiss::test_support::test_files(files));
  [[maybe_unused]] auto& cache = asset_owner.cache();
  render_asset_loader loader(asset_owner, resources);
  const render_asset_reload source_a{"asset://a", render_asset_type::material};
  const render_asset_reload source_b{"asset://b", render_asset_type::material};
  render_asset_ref a, b;
  check(loader.reserve_asset(source_a, a) == gneiss::engine::result::success);
  check(loader.reserve_asset(source_b, b) == gneiss::engine::result::success);
  gneiss::test_support::fixture_files different;
  check(different.mount("asset://", std::make_shared<memory_files>()) ==
        gneiss::engine::result::success);
  asset_manager isolated_owner(gneiss::test_support::test_files(different));
  render_asset_loader isolated(isolated_owner, resources);
  render_asset_ref conflict;
  check(isolated.reserve_asset(source_a, conflict) == gneiss::engine::result::success &&
        conflict != a && gneiss::engine::asset_rid(conflict) != gneiss::engine::asset_rid(a));
  check(!isolated.owns(a));
  render_asset_loader shared(asset_owner, resources);
  check(shared.reserve_asset(source_a, conflict) == gneiss::engine::result::success &&
        gneiss::engine::asset_rid(conflict) == gneiss::engine::asset_rid(a));

  const auto stage = [&](const auto& source, const auto& target, auto sequence, auto& output) {
    prepared_render_asset prepared;
    prepared.source = source;
    return loader.prepare_update(std::move(prepared), output, target, sequence);
  };
  std::array<render_asset_loader::asset_update, 1> first, unrelated, late, latest;
  const auto first_sequence = loader.next_update_sequence();
  check(stage(source_a, a, first_sequence, first.front()) == gneiss::engine::result::success);
  check(stage(source_b, b, loader.next_update_sequence(), unrelated.front()) ==
        gneiss::engine::result::success);
  check(loader.apply_update(unrelated.front()) == gneiss::engine::result::success);
  check(loader.apply_update(first.front()) == gneiss::engine::result::success);
  const auto previous = resources.share_material(gneiss::engine::asset_rid(a));
  const auto old_sequence = loader.next_update_sequence();
  check(stage(source_a, a, old_sequence, late.front()) == gneiss::engine::result::success);
  const auto new_sequence = loader.next_update_sequence();
  check(loader.claim_update(a, new_sequence));
  // 新请求尚未完成也阻止旧任务覆盖；最新请求失败时仍保留当前成功内容。
  check(loader.apply_update(late.front()) == gneiss::engine::result::invalid_state);
  check(resources.share_material(gneiss::engine::asset_rid(a)) == previous);
  check(stage(source_a, a, old_sequence, late.front()) == gneiss::engine::result::invalid_state);
  check(stage(source_a, a, new_sequence, latest.front()) == gneiss::engine::result::success);
  check(loader.apply_update(latest.front()) == gneiss::engine::result::success);
}
void reserved_material_lifetime() {
  auto source = std::make_shared<memory_files>();
  gneiss::test_support::fixture_files files;
  check(files.mount("asset://", source) == gneiss::engine::result::success);
  render_resource_service resources;
  gneiss::engine::asset_manager asset_owner(gneiss::test_support::test_files(files));
  [[maybe_unused]] auto& cache = asset_owner.cache();
  render_asset_loader loader(asset_owner, resources);
  const render_asset_reload request{"asset://material", render_asset_type::material};
  render_asset_ref target;
  check(loader.reserve_asset(request, target) == gneiss::engine::result::success);
  const auto rid = gneiss::engine::asset_rid(target);
  prepared_render_asset prepared;
  prepared.source = request;
  std::array<render_asset_loader::asset_update, 1> candidates;
  check(loader.prepare_update(std::move(prepared), candidates.front(), target) ==
        gneiss::engine::result::success);
  check(resources.material_status(rid) == gneiss::engine::result::not_ready);
  check(loader.apply_update(candidates.front()) == gneiss::engine::result::success);
  candidates = {};
  check(resources.material_status(rid) == gneiss::engine::result::success);
  target = {};
  loader.release_unused();
  check(resources.material_status(rid) == gneiss::engine::result::invalid_handle);
}
void cpu_loading(execution_mode mode) {
  task_scheduler scheduler({.mode = mode});
  auto backend = std::make_shared<delayed_backend>();
  auto files = std::make_shared<file_service>(
      file_service_options{.max_file_bytes = texture_load_service::maximum_bytes});
  check(files->mount({.prefix = "asset://", .readers = {backend}, .writer = {}}).ok());
  render_resource_service resources;
  asset_manager manager(gneiss::test_support::test_files(files));
  render_asset_loader loader(manager, resources);
  texture_load_service service(scheduler, loader);
  memory_files fixture;
  fixture.pixel(std::byte{42});
  fixture.text(
      "texture",
      R"({"format":"gneiss.texture","version":1,"source":"asset://image.ktx2","color_space":"srgb"})");
  const std::array<std::string, 1> uris{"asset://texture"};
  std::uint64_t request{};
  texture_load_completion completed;
  auto advance = [&] {
    (void)scheduler.run_ready();
    check(files->dispatch_completions(64).ok());
    manager.advance_resources();
    service.advance();
    std::this_thread::yield();
  };
  auto until = [&](auto predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!predicate()) {
      check(std::chrono::steady_clock::now() < deadline);
      advance();
    }
  };
  check(service.submit(uris, 1U, 1U, request).ok());
  auto target = service.requested_assets().front();
  until([&] { return bool(backend->callback); });
  check(resources.texture_status(asset_rid(target)) == result::not_ready);
  check(!service.take(completed));
  backend->complete(fixture.files.at("texture"));
  until([&] { return bool(backend->callback); });
  check(backend->path == "image.ktx2");
  backend->complete(fixture.files.at("image.ktx2"));
  until([&] { return service.take(completed); });
  // 没有 Render 或 GPU 回执，CPU 数据就绪即成功。
  check(completed.result.ok() && completed.state == texture_load_state::applied);
  check(resources.texture_status(asset_rid(target)).ok());
  const auto previous = resources.share_texture(asset_rid(target));
  check(previous && previous->levels.front().pixels.front() == std::byte{42});
  const auto reads = backend->reads;
  check(service.submit(uris, 1U, 2U, request, false).ok());
  until([&] { return service.take(completed); });
  check(completed.result.ok() && backend->reads == reads);
  check(service.submit(uris, 1U, 3U, request).ok());
  until([&] { return bool(backend->callback); });
  backend->complete({}, result::io);
  until([&] { return service.take(completed); });
  check(completed.result == result::io && resources.share_texture(asset_rid(target)) == previous);
  check(service.submit(uris, 1U, 4U, request).ok());
  until([&] { return bool(backend->callback); });
  check(service.cancel());
  until([&] { return service.take(completed); });
  check(completed.state == texture_load_state::cancelled);
  backend->complete(fixture.files.at("texture"));
  check(files->dispatch_completions(64U).ok());
  check(resources.share_texture(asset_rid(target)) == previous);
  fixture.text(
      "mesh",
      R"({"format":"gneiss.mesh","version":1,"topology":"triangle_list","vertices":[[0,0,0],[1,0,0],[0,1,0]]})");
  fixture.text(
      "material",
      R"({"format":"gneiss.material","version":5,"metallic":0,"roughness":1,"color":[1,1,1,1],"base_color_texture":"asset://texture"})");
  const std::array mixed{render_asset_reload{"asset://mesh", render_asset_type::mesh},
                         render_asset_reload{"asset://material", render_asset_type::material}};
  check(service.submit_assets(mixed, 1U, 5U, request).ok());
  auto mesh_target = service.requested_assets().front();
  auto material_target = service.requested_assets().back();
  until([&] {
    if (backend->callback)
      backend->complete(fixture.files.at(backend->path));
    return service.take(completed);
  });
  auto material = asset_cast<material_resource>(material_target);
  check(completed.result.ok() && resources.mesh_status(asset_rid(mesh_target)).ok());
  check(material && material->textures()[0] == asset_cast<texture_resource>(target));
  // 已采用网格之后取消后续材质读取，不回滚成功项或既有依赖。
  check(service.submit_assets(mixed, 1U, 6U, request).ok());
  until([&] {
    if (backend->callback && backend->path == "mesh")
      backend->complete(fixture.files.at("mesh"));
    return backend->callback && backend->path == "material";
  });
  const auto adopted_mesh = resources.share_mesh(asset_rid(mesh_target));
  check(service.cancel());
  until([&] { return service.take(completed); });
  check(completed.state == texture_load_state::cancelled && completed.results.front().applied);
  backend->complete(fixture.files.at("material"));
  check(files->dispatch_completions(64U).ok());
  check(resources.share_mesh(asset_rid(mesh_target)) == adopted_mesh);
  // CPU 字节预算失败不破坏已采用内容。
  check(service.submit_assets(std::span(mixed).first(1U), 1U, 7U, request, true, 1U).ok());
  until([&] {
    if (backend->callback)
      backend->complete(fixture.files.at(backend->path));
    return service.take(completed);
  });
  check(completed.result.failed() && resources.share_mesh(asset_rid(mesh_target)) == adopted_mesh);
}
} // namespace
int main() try {
  prepared_source_revisions();
  mesh_data_lifetime();
  independent_update_sequences();
  reserved_material_lifetime();
  cpu_loading(execution_mode::cooperative);
  cpu_loading(execution_mode::thread_pool);
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
