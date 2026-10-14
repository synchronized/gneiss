// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/asset/texture_container.hpp"
#include "engine/asset/texture_ktx2.hpp"
#include "engine/function/render/render_resource_service.hpp"
#include "engine/function/render/texture_load_service.hpp"
#include <algorithm>
#include <granit/asset_tools/texture_builder.hpp>

#include <atomic>
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
  std::shared_ptr<std::atomic_size_t> bytes_read = std::make_shared<std::atomic_size_t>();
  gneiss_result open_read(std::string_view path,
                          std::unique_ptr<read_source>& output) const noexcept override {
    reader = std::this_thread::get_id();
    output.reset();
    const auto found = files.find(std::string(path));
    if (found == files.end()) {
      return GNEISS_ERROR_NOT_FOUND;
    }
    class memory_source final : public read_source {
    public:
      explicit memory_source(std::vector<std::byte> bytes,
                             std::shared_ptr<std::atomic_size_t> bytes_read)
          : bytes_(std::move(bytes)), bytes_read_(std::move(bytes_read)) {}
      std::uint64_t size() const noexcept override { return bytes_.size(); }
      gneiss_result read_at(std::uint64_t offset,
                            std::span<std::byte> output) const noexcept override {
        if (offset > bytes_.size() || output.size() > bytes_.size() - offset) {
          return GNEISS_ERROR_IO;
        }
        std::ranges::copy(
            std::span{bytes_}.subspan(static_cast<std::size_t>(offset), output.size()),
            output.begin());
        bytes_read_->fetch_add(output.size(), std::memory_order_relaxed);
        return GNEISS_SUCCESS;
      }

    private:
      std::vector<std::byte> bytes_;
      std::shared_ptr<std::atomic_size_t> bytes_read_;
    };
    try {
      output = std::make_unique<memory_source>(found->second, bytes_read);
    } catch (...) {
      return GNEISS_ERROR_OUT_OF_MEMORY;
    }
    return GNEISS_SUCCESS;
  }
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
void cooperative_read_slices() {
  tasks::task_scheduler scheduler({.mode = tasks::execution_mode::cooperative});
  auto files = std::make_shared<memory_files>();
  files->text(
      "large.texture.json",
      R"({"format":"gneiss.texture","version":1,"source":"asset://image.ktx2","color_space":"srgb"})");
  std::string diagnostic;
  texture_ktx2 input{.transfer = texture_transfer::srgb, .levels = {}};
  for (std::uint32_t dimension = 1024U; dimension != 0U; dimension /= 2U) {
    input.levels.push_back({
        .width = dimension,
        .height = dimension,
        .pixels = std::vector<std::byte>(std::size_t{4U} * dimension * dimension, std::byte{127}),
    });
  }
  check(encode_texture_ktx2(input, files->files["image.ktx2"], diagnostic) ==
        texture_ktx2_result::success);
  virtual_file_system vfs;
  check(vfs.mount("asset://", files) == GNEISS_SUCCESS);
  render_resource_service resources;
  resource_cache cache;
  render_asset_loader loader(vfs, cache, resources);
  texture_upload_backend backend{
      .begin =
          [](const auto&, auto& sequence) {
            sequence = 1U;
            return GNEISS_SUCCESS;
          },
      .poll =
          [](auto, auto& result) {
            result = GNEISS_SUCCESS;
            return true;
          },
      .discard =
          [](const auto&, auto& sequence) {
            sequence = 2U;
            return GNEISS_SUCCESS;
          },
      .flush = [] {},
  };
  texture_load_service service(scheduler, vfs, loader, std::move(backend));
  const std::vector<std::string> uris{"asset://large.texture.json"};
  std::uint64_t request{};
  check(service.submit(uris, 1U, 1U, request) == GNEISS_SUCCESS);
  texture_load_completion completion;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  std::size_t reading_tasks{};
  while (!service.take(completion)) {
    check(std::chrono::steady_clock::now() < deadline);
    const auto before = files->bytes_read->load();
    (void)scheduler.run_ready({.max_tasks = 1U});
    const auto consumed = files->bytes_read->load() - before;
    // 验证服务真实消费量，而不是只检查预算常量；大载荷仍须完整分段准备和复验。
    check(consumed <= std::size_t{4U} * 1024U * 1024U);
    reading_tasks += consumed != 0U ? 1U : 0U;
    service.advance();
  }
  // 两遍约 5.3 MiB 的载荷至少需要三块；相邻阶段允许共用剩余预算。
  if (reading_tasks < 3U || completion.state != texture_load_state::applied) {
    throw std::runtime_error("大纹理分片失败：读取任务=" + std::to_string(reading_tasks) +
                             "，结果=" + std::to_string(completion.result) +
                             "，消息=" + completion.message);
  }
  check(files->bytes_read->load() >= 2U * files->files["image.ktx2"].size());
  check(completion.textures.size() == 1U && cache.size() == 1U);
  const auto texture = resources.share_texture(completion.textures.front().get());
  check(texture && texture->levels.front().pixels.size() == std::size_t{4U} * 1024U * 1024U &&
        texture->levels.front().pixels.back() == std::byte{127});
}

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
void mixed(tasks::execution_mode mode, bool pbr = false) {
  tasks::task_scheduler scheduler({.workers = 1U, .mode = mode});
  auto files = std::make_shared<memory_files>();
  files->pixel(std::byte{10});
  files->text(
      "a.texture.json",
      R"({"format":"gneiss.texture","version":1,"source":"asset://image.ktx2","color_space":"srgb"})");
  files->text(
      "m.material.json",
      R"({"format":"gneiss.material","version":3,"color":[1,1,1,1],"base_color_texture":"asset://a.texture.json","metallic":0,"roughness":1})");
  if (pbr) {
    for (unsigned i = 1; i < 5U; ++i) {
      files->text(
          std::to_string(i) + ".texture.json",
          R"({"format":"gneiss.texture","version":1,"source":"asset://image.ktx2","color_space":"srgb"})");
    }
    files->text(
        "m.material.json",
        R"({"format":"gneiss.material","version":4,"color":[1,1,1,1],"base_color_texture":"asset://a.texture.json","metallic":0,"roughness":1,"metallic_roughness_texture":"asset://1.texture.json","normal_texture":"asset://2.texture.json","occlusion_texture":"asset://3.texture.json","emissive_texture":"asset://4.texture.json","emissive":[0.2,0.3,0.4]})");
  }
  const auto expected_assets = pbr ? 11U : 7U;
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
            for (const auto& item : data) {
              bytes += item.bytes;
            }
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
            check(data.size() == expected_assets);
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
  check(completion.state == texture_load_state::applied &&
        completion.assets.size() == expected_assets);
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
  check(service.progress(progress) && !progress.can_cancel &&
        progress.total_assets == expected_assets && !service.cancel());
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
  if (pbr) {
    check(material->emissive[2] == 0.4F);
    for (const auto texture : material->texture_handles()) {
      check(texture != GNEISS_NULL_TEXTURE && resources.get_texture(texture) != nullptr);
    }
    // 丢失附加纹理时不得发布部分材质，也不能替换已有版本。
    const auto retained = resources.share_material(material_lease.get());
    files->files.erase("3.texture.json");
    check(service.submit_assets(requested, 1U, 4U, request) == GNEISS_SUCCESS);
    until([&] { return service.take(completion); });
    check(completion.state == texture_load_state::failed &&
          resources.share_material(material_lease.get()) == retained);
  }
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
void packaged_lifetime(tasks::execution_mode mode) {
#if defined(GNEISS_HAS_GRANIT_PLATFORM)
  tasks::task_scheduler scheduler({.workers = 1U, .mode = mode});
  auto files = std::make_shared<memory_files>();
  const std::array payload{std::byte{7}, std::byte{8}, std::byte{9}, std::byte{255}};
  const std::array mips{granit::asset_tools::texture::subresource_info{
      .data_size = 4U, .bytes_per_row = 4U, .rows_per_image = 1U}};
  const std::array variants{granit::asset_tools::texture::variant_desc{
      .format = granit::texture_format::rgba8_srgb,
      .usage = granit::texture_usage::sampled | granit::texture_usage::transfer_destination,
      .payload = payload,
      .subresources = mips}};
  const auto [status, built] = granit::asset_tools::texture::build({.variants = variants});
  check(status == granit::result::success);
  std::string message;
  check(encode_texture_binary(built.manifest(), built.payload(),
                              files->files["image.gneiss-texture"],
                              message) == texture_binary_result::success);
  files->text(
      "a.texture.json",
      R"({"format":"gneiss.texture","version":1,"source":"asset://image.gneiss-texture","color_space":"srgb"})");
  virtual_file_system vfs;
  check(vfs.mount("asset://", files) == GNEISS_SUCCESS);
  render_resource_service resources;
  resource_cache cache;
  render_asset_loader loader(vfs, cache, resources);
  texture_upload_backend::data in_flight;
  std::weak_ptr<const std::vector<std::byte>> observed;
  bool ack{};
  bool oversized{true};
  auto upload_result = GNEISS_ERROR_INITIALIZATION_FAILED;
  texture_upload_backend backend{
      .begin =
          [&](auto data, auto& sequence) {
            check(data.size() == 1U && data.front().texture_payload &&
                  data.front().texture->payload.empty());
            observed = data.front().texture_payload;
            in_flight = std::move(data);
            sequence = 1U;
            return GNEISS_SUCCESS;
          },
      .poll =
          [&](auto, auto& result) {
            if (!ack) {
              return false;
            }
            check(!observed.expired());
            in_flight.clear();
            result = upload_result;
            return true;
          },
      .discard =
          [](auto, auto& sequence) {
            sequence = 2U;
            return GNEISS_SUCCESS;
          },
      .flush = [&] { ack = true; },
      .estimate_bytes =
          [&](const auto&) {
            return oversized ? texture_load_service::maximum_upload_bytes + 1U : payload.size();
          },
      .profile = {.generation = 1U, .sampled_transfer_formats = {true, true, false, false}}};
  auto service = std::make_unique<texture_load_service>(scheduler, vfs, loader, std::move(backend));
  const auto until = [&](auto predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!predicate()) {
      check(std::chrono::steady_clock::now() < deadline);
      if (mode == tasks::execution_mode::cooperative) {
        (void)scheduler.run_ready();
      }
      service->advance();
      std::this_thread::yield();
    }
  };
  const std::array<std::string, 1> uris{"asset://a.texture.json"};
  std::uint64_t request{};
  texture_load_completion completion;
  // 超出硬上限不得调用上传后端，也不得发布候选。
  check(service->submit(uris, 1U, 1U, request) == GNEISS_SUCCESS);
  until([&] { return service->take(completion); });
  check(completion.result == GNEISS_ERROR_OUT_OF_MEMORY && in_flight.empty() &&
        observed.expired() && cache.size() == 0U && !completion.message.empty());
  oversized = false;
  // 失败回执前后分别验证强租约存活与销毁，并确认可重试。
  check(service->submit(uris, 1U, 1U, request) == GNEISS_SUCCESS);
  until([&] { return !in_flight.empty(); });
  check(!observed.expired() && !service->cancel());
  asset_load_progress progress;
  check(service->progress(progress) && progress.upload_reserved_bytes == payload.size());
  ack = true;
  until([&] { return service->take(completion); });
  check(completion.state == texture_load_state::failed && observed.expired() && cache.size() == 0U);
  upload_result = GNEISS_SUCCESS;
  ack = false;
  check(service->submit(uris, 1U, 2U, request) == GNEISS_SUCCESS);
  until([&] { return !in_flight.empty(); });
  const auto old_frame = in_flight.front().texture;
  ack = true;
  until([&] { return service->take(completion); });
  check(completion.state == texture_load_state::applied && observed.expired() &&
        completion.peak_upload_bytes == payload.size());
  check(!service->progress(progress) && progress.upload_reserved_bytes == 0U);
  check(old_frame->payload.empty() && old_frame->upload_payload.expired());
  std::vector<std::byte> restored;
  check(old_frame->payload_source->read(restored, payload.size()) == GNEISS_SUCCESS &&
        std::ranges::equal(restored, payload));
  // 上传中关闭必须先等待回执；旧帧只保留元数据，不阻止负载租约销毁。
  ack = false;
  check(service->submit(uris, 1U, 3U, request) == GNEISS_SUCCESS);
  until([&] { return !in_flight.empty(); });
  service.reset();
  check(in_flight.empty() && observed.expired());
#else
  (void)mode;
#endif
}
}
namespace {
void cancel_unpublished_upload(tasks::execution_mode mode) {
  tasks::task_scheduler scheduler({.workers = 1U, .mode = mode});
  auto files = std::make_shared<memory_files>();
  files->pixel(std::byte{10});
  for (const auto* path : {"a.texture.json", "b.texture.json"}) {
    files->text(
        path,
        R"({"format":"gneiss.texture","version":1,"source":"asset://image.ktx2","color_space":"srgb"})");
  }
  virtual_file_system vfs;
  check(vfs.mount("asset://", files) == GNEISS_SUCCESS);
  render_resource_service resources;
  resource_cache cache;
  render_asset_loader loader(vfs, cache, resources);
  unsigned uploads{};
  unsigned discards{};
  bool upload_ack{};
  bool discard_ack{};
  texture_upload_backend::data retained;
  texture_upload_backend backend{
      .begin =
          [&](auto data, auto& sequence) {
            check(data.size() == 1U);
            retained = std::move(data);
            sequence = 1U;
            ++uploads;
            return GNEISS_SUCCESS;
          },
      .poll =
          [&](auto sequence, auto& result) {
            const bool ready = sequence == 1U ? upload_ack : discard_ack;
            if (ready) {
              retained.clear();
              result = GNEISS_SUCCESS;
            }
            return ready;
          },
      .discard =
          [&](auto data, auto& sequence) {
            check(data.size() == 2U);
            retained = std::move(data);
            sequence = 2U;
            ++discards;
            return GNEISS_SUCCESS;
          },
      .flush = [&] { upload_ack = discard_ack = true; },
      .estimate_bytes = [](const auto&) { return texture_load_service::upload_budget_bytes; },
  };
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
  const std::array<std::string, 2> uris{"asset://a.texture.json", "asset://b.texture.json"};
  std::uint64_t request{};
  texture_load_completion completion;
  check(service.submit(uris, 1U, 1U, request, false) == GNEISS_SUCCESS);
  until([&] { return uploads == 1U; });
  check(!service.cancel() && service.cancel_unpublished() && service.cancel_unpublished());
  advance();
  check(!service.take(completion) && uploads == 1U && discards == 0U && !retained.empty());
  upload_ack = true;
  until([&] { return discards == 1U; });
  advance();
  check(!service.take(completion) && uploads == 1U && cache.size() == 0U && !retained.empty());
  discard_ack = true;
  until([&] { return service.take(completion); });
  check(completion.state == texture_load_state::cancelled && retained.empty() &&
        resources.live_resource_count() == 0U && !service.cancel_unpublished());
  check(service.submit(uris, 1U, 2U, request, false) == GNEISS_SUCCESS);
  until([&] { return service.take(completion); });
  check(completion.state == texture_load_state::applied && uploads == 3U && discards == 1U &&
        cache.size() == 2U);
}
} // namespace

int main() try {
  cooperative_read_slices();
  cancel_unpublished_upload(tasks::execution_mode::cooperative);
  cancel_unpublished_upload(tasks::execution_mode::thread_pool);
  packaged_lifetime(tasks::execution_mode::cooperative);
  packaged_lifetime(tasks::execution_mode::thread_pool);
  mixed(tasks::execution_mode::cooperative, true);
  mixed(tasks::execution_mode::thread_pool, true);
  mixed(tasks::execution_mode::cooperative);
  mixed(tasks::execution_mode::thread_pool);
  run(tasks::execution_mode::cooperative);
  run(tasks::execution_mode::thread_pool);
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
