// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "asset/texture_ktx2.h"
#include "asset/texture_load_service.h"

#include <cstdio>
#include <map>
#include <source_location>

namespace {
using namespace gneiss;
using namespace asset_internal;
using namespace render_internal;
void check(bool value, std::source_location at = std::source_location::current()) {
  if (!value) {
    throw std::runtime_error("异步资产契约失败，行=" + std::to_string(at.line()));
  }
}
struct memory_files final : file_system {
  std::map<std::string, std::vector<std::byte>> files;
  mutable std::thread::id reader;
  gneiss_result read(std::string_view path, std::vector<std::byte>& bytes) const noexcept override {
    return read_bounded(path, SIZE_MAX, bytes);
  }
  gneiss_result read_bounded(std::string_view path, std::size_t limit,
                             std::vector<std::byte>& bytes) const noexcept override {
    reader = std::this_thread::get_id();
    const auto found = files.find(std::string(path));
    if (found == files.end()) {
      return GNEISS_ERROR_NOT_FOUND;
    }
    if (found->second.size() > limit) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    bytes = found->second;
    return GNEISS_SUCCESS;
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
void run(tasks::execution_mode mode) {
  tasks::task_scheduler scheduler({.workers = 2U, .mode = mode});
  auto files = std::make_shared<memory_files>();
  files->text(
      "a.texture.json",
      R"({"format":"gneiss.texture","version":1,"source":"asset://image.ktx2","color_space":"srgb"})");
  files->text(
      "b.texture.json",
      R"({"format":"gneiss.texture","version":1,"source":"asset://missing.ktx2","color_space":"srgb"})");
  files->pixel(std::byte{10});
  virtual_file_system vfs;
  check(vfs.mount("asset://", files) == GNEISS_SUCCESS);
  render_resource_service resources;
  resource_cache cache;
  render_asset_loader loader(vfs, cache, resources);
  bool uploaded{};
  bool ready{};
  gneiss_result upload_result = GNEISS_SUCCESS;
  unsigned discarded{};
  texture_upload_backend backend{.begin =
                                     [&](auto, auto& sequence) {
                                       uploaded = true;
                                       sequence = 1U;
                                       return GNEISS_SUCCESS;
                                     },
                                 .poll =
                                     [&](auto, auto& result) {
                                       result = upload_result;
                                       return ready;
                                     },
                                 .discard =
                                     [&](auto, auto& sequence) {
                                       ++discarded;
                                       sequence = 2U;
                                       return GNEISS_SUCCESS;
                                     },
                                 .flush = [&] { ready = true; }};
  texture_load_service service(scheduler, vfs, loader, std::move(backend));
  const auto advance = [&] {
    if (mode == tasks::execution_mode::cooperative) {
      (void)scheduler.run_ready();
    }
    service.advance();
  };
  const auto until = [&](auto predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!predicate()) {
      check(std::chrono::steady_clock::now() < deadline);
      advance();
      std::this_thread::yield();
    }
  };
  const std::vector<std::string> uris{"asset://a.texture.json"};
  std::uint64_t request{};
  check(service.submit(uris, 1U, 1U, request) == GNEISS_SUCCESS && request != 0U);
  std::uint64_t rejected{};
  check(service.submit(uris, 1U, 2U, rejected) == GNEISS_ERROR_NOT_READY);
  until([&] { return uploaded; });
  check(cache.size() == 0U);
  check((files->reader == std::this_thread::get_id()) ==
        (mode == tasks::execution_mode::cooperative));
  texture_load_completion completion;
  check(!service.take(completion));
  upload_result = GNEISS_ERROR_INITIALIZATION_FAILED;
  ready = true;
  until([&] { return service.take(completion); });
  check(completion.state == texture_load_state::failed && cache.size() == 0U &&
        resources.live_resource_count() == 0U);
  upload_result = GNEISS_SUCCESS;
  check(service.submit(uris, 1U, 2U, request) == GNEISS_SUCCESS);
  until([&] { return service.take(completion); });
  check(completion.state == texture_load_state::applied && completion.textures.size() == 1U);
  auto lease = completion.textures.front();
  const auto rid = lease.get();
  auto previous = resources.share_texture(rid);
  check(previous && previous->levels.front().pixels.front() == std::byte{10});
  files->reader = {};
  check(service.submit(uris, 1U, 2U, request, false) == GNEISS_SUCCESS);
  check(service.take(completion) && completion.textures.front().get() == rid &&
        files->reader == std::thread::id{});
  files->pixel(std::byte{20});
  uploaded = false;
  ready = false;
  check(service.submit(uris, 1U, 3U, request) == GNEISS_SUCCESS);
  until([&] { return uploaded; });
  check(resources.share_texture(rid) == previous);
  service.cancel(); // 上传已获许可，迟到取消不撤销提交。
  ready = true;
  until([&] { return service.take(completion); });
  check(completion.state == texture_load_state::applied &&
        completion.textures.front().get() == rid && resources.share_texture(rid) != previous &&
        previous->levels.front().pixels.front() == std::byte{10});
  previous = resources.share_texture(rid);
  const std::vector<std::string> broken{uris.front(), "asset://b.texture.json"};
  check(service.submit(broken, 1U, 4U, request) == GNEISS_SUCCESS);
  until([&] { return service.take(completion); });
  check(completion.state == texture_load_state::failed &&
        resources.share_texture(rid) == previous && cache.size() == 1U);
  // GPU 已完成但缓存身份被同步事务替换：丢弃候选，不能覆盖新的缓存。
  uploaded = false;
  ready = false;
  check(service.submit(uris, 1U, 5U, request) == GNEISS_SUCCESS);
  until([&] { return uploaded; });
  asset_diagnostic diagnostic;
  const std::vector<render_asset_reload> synchronous{{uris.front(), render_asset_type::texture}};
  check(loader.reload_assets(synchronous, diagnostic) == GNEISS_SUCCESS);
  texture_asset_lease newer;
  check(loader.acquire_texture(uris.front(), newer, diagnostic) == GNEISS_SUCCESS);
  const auto newer_rid = newer.get();
  ready = true;
  until([&] { return service.take(completion); });
  check(completion.state == texture_load_state::failed && discarded == 1U &&
        resources.share_texture(rid) == previous);
  lease = std::move(newer);
  // 未获提交许可的卸载不能被迟到候选复活。
  check(service.submit(uris, 1U, 5U, request) == GNEISS_SUCCESS);
  lease = {};
  completion = {};
  cache.release_unused();
  check(resources.get_texture(newer_rid) == nullptr);
  until([&] { return service.take(completion); });
  check(completion.state == texture_load_state::failed && cache.size() == 0U);
  check(service.submit(uris, 2U, 1U, request) == GNEISS_SUCCESS);
  service.cancel();
  until([&] { return service.take(completion); });
  check(completion.state == texture_load_state::cancelled && resources.live_resource_count() == 0U);
  service.request_stop();
  check(service.stopped() && discarded == 1U);
}
void mixed(tasks::execution_mode mode) {
  tasks::task_scheduler scheduler({.workers = 1U, .mode = mode});
  auto files = std::make_shared<memory_files>();
  files->pixel(std::byte{10});
  files->text(
      "a.texture.json",
      R"({"format":"gneiss.texture","version":1,"source":"asset://image.ktx2","color_space":"srgb"})");
  files->text(
      "m.material.json",
      R"({"format":"gneiss.material","version":3,"color":[1,1,1,1],"base_color_texture":"asset://a.texture.json","metallic":0,"roughness":1})");
  const std::string mesh =
      R"({"format":"gneiss.mesh","version":3,"topology":"triangle_list","vertices":[[0,0,0],[1,0,0],[0,1,0]],"uvs":[[0,0],[1,0],[0,1]],"normals":[[0,0,1],[0,0,1],[0,0,1]]})";
  std::vector<render_asset_reload> requested{
      {"asset://m.material.json", render_asset_type::material}};
  for (unsigned i = 0; i < 5U; ++i) {
    const auto name = std::to_string(i) + ".mesh.json";
    files->text(name, mesh);
    requested.push_back({"asset://" + name, render_asset_type::mesh});
  }
  virtual_file_system vfs;
  check(vfs.mount("asset://", files) == GNEISS_SUCCESS);
  render_resource_service resources;
  resource_cache cache;
  render_asset_loader loader(vfs, cache, resources);
  unsigned chunks{}, discards{};
  bool fail = true;
  bool uploaded = false;
  bool ack = true;
  std::uint64_t serial{};
  texture_upload_backend backend{
      .begin =
          [&](auto data, auto& sequence) {
            check(!data.empty() && data.size() <= 4U);
            std::size_t bytes{};
            for (const auto& item : data)
              bytes += item.bytes;
            check(data.size() == 1U || bytes <= texture_load_service::upload_budget_bytes);
            ++chunks;
            uploaded = true;
            sequence = ++serial;
            return GNEISS_SUCCESS;
          },
      .poll =
          [&](auto, auto& result) {
            result = fail && chunks == 2U && discards == 0U ? GNEISS_ERROR_IO : GNEISS_SUCCESS;
            return ack;
          },
      .discard =
          [&](auto data, auto& sequence) {
            check(data.size() == 7U);
            ++discards;
            sequence = ++serial;
            return GNEISS_SUCCESS;
          },
      .flush = [&] { ack = true; },
      .estimate_bytes = [](const auto& item) -> std::size_t {
        return item.mesh ? 5U * 1024U * 1024U : 1U * 1024U * 1024U;
      }};
  texture_load_service service(scheduler, vfs, loader, std::move(backend));
  const auto advance = [&] {
    if (mode == tasks::execution_mode::cooperative)
      (void)scheduler.run_ready();
    service.advance();
  };
  const auto until = [&](auto predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!predicate()) {
      check(std::chrono::steady_clock::now() < deadline);
      advance();
      std::this_thread::yield();
    }
  };
  std::uint64_t request{};
  texture_load_completion completion;
  check(service.submit_assets(requested, 1U, 1U, request, false, 0U) ==
        GNEISS_ERROR_INVALID_ARGUMENT);
  check(service.submit_assets(requested, 1U, 1U, request, false, 8U) == GNEISS_SUCCESS);
  until([&] { return service.take(completion); });
  check(completion.state == texture_load_state::failed && chunks == 0U && cache.size() == 0U &&
        resources.live_resource_count() == 0U);
  check(service.submit_assets(requested, 1U, 1U, request) == GNEISS_SUCCESS);
  until([&] { return service.take(completion); });
  check(completion.state == texture_load_state::failed && discards == 1U && chunks == 2U &&
        cache.size() == 0U && resources.live_resource_count() == 0U);
  fail = false;
  check(service.submit_assets(requested, 1U, 2U, request) == GNEISS_SUCCESS);
  check(service.cancel());
  until([&] { return service.take(completion); });
  check(completion.state == texture_load_state::cancelled && cache.size() == 0U);

  check(service.submit_assets(requested, 1U, 2U, request) == GNEISS_SUCCESS);
  until([&] { return service.take(completion); });
  check(completion.state == texture_load_state::applied && completion.assets.size() == 7U);
  auto leases = completion.assets;
  render_asset_lease mesh_lease, material_lease;
  check(loader.acquire_cached(requested[1], mesh_lease) == GNEISS_SUCCESS);
  check(loader.acquire_cached(requested[0], material_lease) == GNEISS_SUCCESS);
  const auto old_mesh = resources.share_mesh(mesh_lease.get());
  const auto old_material = resources.share_material(material_lease.get());
  auto changed = mesh;
  changed.replace(changed.find("[1,0,0]"), 7U, "[2,0,0]");
  files->text("0.mesh.json", changed);
  files->pixel(std::byte{200});
  uploaded = false;
  ack = false;
  check(service.submit_assets(requested, 1U, 3U, request) == GNEISS_SUCCESS);
  until([&] { return uploaded; });
  asset_load_progress progress;
  check(service.progress(progress) && !progress.can_cancel && progress.total_assets == 7U &&
        !service.cancel());
  check(resources.share_mesh(mesh_lease.get()) == old_mesh &&
        resources.share_material(material_lease.get()) == old_material);
  ack = true;
  until([&] { return service.take(completion); });
  check(completion.state == texture_load_state::applied &&
        resources.share_mesh(mesh_lease.get()) != old_mesh && old_mesh->vertices[1].x == 1.0F &&
        resources.get_mesh(mesh_lease.get())->vertices[1].x == 2.0F);
  completion = {};
  leases.clear();
  cache.release_unused();
  const auto* material = resources.get_material(material_lease.get());
  check(material && resources.get_texture(material->base_color_texture) != nullptr);
  // 同 URI 类型冲突、依赖缺失、闭包容量限制都必须在发布前拒绝。
  prepared_render_batch prepared;
  asset_diagnostic diagnostic;
  auto conflicting = requested;
  conflicting.push_back({"asset://a.texture.json", render_asset_type::mesh});
  check(prepare_render_assets(vfs, conflicting, prepared, diagnostic, [] { return false; }) !=
        GNEISS_SUCCESS);
  check(prepare_render_assets(
            vfs, requested, prepared, diagnostic, [] { return false; }, 2U) != GNEISS_SUCCESS);
  check(prepare_render_assets(vfs, requested, prepared, diagnostic, [] { return true; }) !=
        GNEISS_SUCCESS);
  check(prepare_render_assets(
            vfs, requested, prepared, diagnostic, [] { return false; }, 256U, 8U) !=
        GNEISS_SUCCESS);
  files->files.erase("image.ktx2");
  check(prepare_render_assets(vfs, requested, prepared, diagnostic, [] { return false; }) !=
        GNEISS_SUCCESS);
}
}
int main() try {
  mixed(tasks::execution_mode::cooperative);
  mixed(tasks::execution_mode::thread_pool);
  run(tasks::execution_mode::cooperative);
  run(tasks::execution_mode::thread_pool);
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
