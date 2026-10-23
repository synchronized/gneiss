// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/render/render_resource_service.hpp"
#include "engine/function/render/texture_load_service.hpp"
#include "engine/resource/cache/cpu_document_cache.hpp"
#include "engine/resource/loaders/texture_container.hpp"
#include "engine/resource/loaders/texture_ktx2.hpp"
#include "engine/resource/types/material_document.hpp"
#include <algorithm>
#include <granit/asset_tools/texture_builder.hpp>

#include <atomic>
#include <cstdio>
#include <map>
#include <source_location>

namespace {
using namespace gneiss;
using namespace engine::resource;
using namespace engine::render;
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
void prepared_source_revisions() {
  auto source = std::make_shared<memory_files>();
  source->pixel(std::byte{10});
  source->files["payload/image.ktx2"] = source->files.at("image.ktx2");
  source->text(
      "texture",
      R"({"format":"gneiss.texture","version":1,"source":"asset://payload/image.ktx2","color_space":"srgb"})");
  source->text(
      "material",
      R"({"format":"gneiss.material","version":2,"color":[1,1,1,1],"base_color_texture":"asset://texture"})");
  virtual_file_system files;
  check(files.mount("asset://", source) == GNEISS_SUCCESS);
  const std::array requests{asset_preparation_source{"asset://material", asset_type::material}};
  prepared_batch before;
  prepared_batch after;
  asset_diagnostic diagnostic;
  check(prepare_assets(files, requests, before, diagnostic, {}, 128U) == GNEISS_SUCCESS);
  prepared_batch repeated;
  check(prepare_assets(files, requests, repeated, diagnostic, {}, 128U) == GNEISS_SUCCESS);
  source->pixel(std::byte{20});
  source->files["payload/image.ktx2"] = source->files.at("image.ktx2");
  check(prepare_assets(files, requests, after, diagnostic, {}, 128U) == GNEISS_SUCCESS);
  check(before.assets.size() == 2U && after.assets.size() == 2U);
  for (std::size_t index = 0; index < before.assets.size(); ++index) {
    const auto& old_asset = before.assets[index];
    const auto& new_asset = after.assets[index];
    check(old_asset.source.uri == new_asset.source.uri && !old_asset.sources.empty());
    check(old_asset.sources.front().identity == new_asset.sources.front().identity);
    check(old_asset.sources.front().digest == new_asset.sources.front().digest);
    if (old_asset.source.type == asset_type::texture) {
      check(old_asset.sources.size() == 2U && new_asset.sources.size() == 2U);
      const auto& payload = new_asset.sources.back();
      asset_identity expected;
      check(files.identify("asset://payload/image.ktx2",
                           static_cast<std::uint32_t>(asset_type::texture),
                           expected) == GNEISS_SUCCESS);
      check(payload.identity == expected && payload.identity == old_asset.sources.back().identity);
      check(payload.bytes == source->files.at("image.ktx2").size());
      check(payload.digest == engine::core::sha256(source->files.at("image.ktx2")));
      check(payload.digest != old_asset.sources.back().digest);
    } else {
      check(old_asset.sources.size() == 1U && new_asset.sources.size() == 1U);
      check(new_asset.texture_uris[0] == "asset://texture");
    }
  }
}
void shared_mesh_documents() {
  auto source = std::make_shared<memory_files>();
  const std::string text =
      R"({"format":"gneiss.mesh","version":3,"topology":"triangle_list","vertices":[[0,0,0],[1,0,0],[0,1,0]],"uvs":[[0,0],[1,0],[0,1]],"normals":[[0,0,1],[0,0,1],[0,0,1]]})";
  source->text("mesh", text);
  virtual_file_system files;
  check(files.mount("asset://", source) == GNEISS_SUCCESS);
  auto documents = std::make_shared<cpu_document_cache>();
  const std::array requests{asset_preparation_source{"asset://mesh", asset_type::mesh}};
  auto prepare = [&](const virtual_file_system& vfs) {
    prepared_batch batch;
    asset_diagnostic diagnostic;
    check(prepare_assets(vfs, requests, batch, diagnostic, {}, 128U) == GNEISS_SUCCESS);
    return batch;
  };
  auto first = prepare(files);
  const auto* vertices = first.assets.front().mesh.vertices.data();
  check(documents->begin()->retain_verified(first) == GNEISS_SUCCESS);
  auto mesh = first.assets.front().shared_mesh;
  check(mesh && mesh->vertices.data() == vertices && first.assets.front().mesh.vertices.empty());
  auto second = prepare(files);
  check(documents->begin()->retain_verified(second) == GNEISS_SUCCESS);
  check(mesh == second.assets.front().shared_mesh);
  virtual_file_system independent;
  check(independent.mount("asset://", source) == GNEISS_SUCCESS);
  auto isolated = prepare(independent);
  check(documents->begin()->retain_verified(isolated) == GNEISS_SUCCESS);
  check(mesh != isolated.assets.front().shared_mesh);
  auto changed_text = text;
  changed_text.replace(changed_text.find("[1,0,0]"), 7, "[2,0,0]");
  source->text("mesh", changed_text);
  auto changed = prepare(files);
  check(documents->begin()->retain_verified(changed) == GNEISS_SUCCESS);
  check(changed.assets.front().shared_mesh != mesh && mesh->vertices[1].x == 1.0F);
  check(changed.assets.front().shared_mesh->vertices[1].x == 2.0F);
  auto invalid = prepare(files);
  invalid.assets.front().sources.front().identity.source.reset();
  check(documents->begin()->retain_verified(invalid) == GNEISS_ERROR_INVALID_STATE);
  render_resource_service resources;
  engine::core::rid rid{};
  check(resources.create_prepared_mesh(mesh, &rid) == GNEISS_SUCCESS);
  check(resources.share_mesh(rid) == mesh);
  auto frame = resources.share_mesh(rid);
  check(resources.destroy_mesh(rid) == GNEISS_SUCCESS && !resources.share_mesh(rid));
  std::weak_ptr<const mesh_data> lifetime = mesh;
  first = {};
  second = {};
  mesh.reset();
  documents->collect();
  documents.reset();
  check(!lifetime.expired() && frame->vertices[1].x == 1.0F);
  frame.reset();
  check(lifetime.expired());
}
void independent_update_sequences() {
  virtual_file_system files;
  check(files.mount("asset://", std::make_shared<memory_files>()) == GNEISS_SUCCESS);
  render_resource_service resources;
  resource_cache cache;
  render_asset_loader loader(files, cache, resources);
  const render_asset_reload source_a{"asset://a", render_asset_type::material};
  const render_asset_reload source_b{"asset://b", render_asset_type::material};
  render_asset_lease a, b;
  check(loader.reserve_asset(source_a, a) == GNEISS_SUCCESS);
  check(loader.reserve_asset(source_b, b) == GNEISS_SUCCESS);
  virtual_file_system different;
  check(different.mount("asset://", std::make_shared<memory_files>()) == GNEISS_SUCCESS);
  render_asset_loader isolated(different, cache, resources);
  render_asset_lease conflict;
  check(isolated.reserve_asset(source_a, conflict) == GNEISS_ERROR_INVALID_HANDLE && !conflict);
  check(!isolated.owns(a));
  const auto copied = files;
  render_asset_loader shared(copied, cache, resources);
  check(shared.reserve_asset(source_a, conflict) == GNEISS_SUCCESS && conflict.get() == a.get());

  const auto stage = [&](const auto& source, const auto& target, auto sequence, auto& output) {
    prepared_render_asset prepared;
    prepared.source = source;
    return loader.prepare_upload(std::move(prepared), output, target, sequence);
  };
  std::array<render_asset_loader::asset_upload, 1> first, unrelated, late, latest;
  const auto first_sequence = loader.next_update_sequence();
  check(stage(source_a, a, first_sequence, first.front()) == GNEISS_SUCCESS);
  check(stage(source_b, b, loader.next_update_sequence(), unrelated.front()) == GNEISS_SUCCESS);
  check(loader.complete_upload(unrelated.front()) == GNEISS_SUCCESS);
  check(loader.complete_upload(first.front()) == GNEISS_SUCCESS);
  const auto previous = resources.share_material(a.get());
  const auto old_sequence = loader.next_update_sequence();
  check(stage(source_a, a, old_sequence, late.front()) == GNEISS_SUCCESS);
  const auto new_sequence = loader.next_update_sequence();
  check(loader.claim_update(a, new_sequence));
  // 新请求尚未完成也阻止旧任务覆盖；最新请求失败时仍保留当前成功内容。
  check(loader.complete_upload(late.front()) == GNEISS_ERROR_INVALID_STATE);
  check(resources.share_material(a.get()) == previous);
  check(stage(source_a, a, old_sequence, late.front()) == GNEISS_ERROR_INVALID_STATE);
  check(stage(source_a, a, new_sequence, latest.front()) == GNEISS_SUCCESS);
  check(loader.complete_upload(latest.front()) == GNEISS_SUCCESS);
}
void reserved_material_lifetime() {
  auto source = std::make_shared<memory_files>();
  virtual_file_system files;
  check(files.mount("asset://", source) == GNEISS_SUCCESS);
  render_resource_service resources;
  resource_cache cache;
  render_asset_loader loader(files, cache, resources);
  const render_asset_reload request{"asset://material", render_asset_type::material};
  render_asset_lease target;
  check(loader.reserve_asset(request, target) == GNEISS_SUCCESS);
  const auto rid = target.get();
  auto document = std::make_shared<material_document>();
  std::weak_ptr<const material_document> lifetime = document;
  prepared_render_asset prepared;
  prepared.source = request;
  prepared.material_document = document;
  std::array<render_asset_loader::asset_upload, 1> candidates;
  check(loader.prepare_upload(std::move(prepared), candidates.front(), target) == GNEISS_SUCCESS);
  document.reset();
  check(!lifetime.expired() && resources.material_status(rid) == GNEISS_ERROR_NOT_READY);
  check(loader.complete_upload(candidates.front()) == GNEISS_SUCCESS);
  candidates = {};
  check(!lifetime.expired() && resources.material_status(rid) == GNEISS_SUCCESS);
  target = {};
  cache.release_unused();
  check(lifetime.expired() && resources.material_status(rid) == GNEISS_ERROR_INVALID_HANDLE);
}
void cooperative_read_slices() {
  engine::core::tasks::task_scheduler scheduler(
      {.mode = engine::core::tasks::execution_mode::cooperative});
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
  texture_load_service service(scheduler, vfs, loader, std::move(backend), {},
                               std::make_shared<cpu_document_cache>());
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
  check(completion.assets.size() == 1U &&
        completion.assets.front().type() == gneiss::engine::render::render_asset_type::texture &&
        cache.size() == 1U);
  const auto texture = resources.share_texture(completion.assets.front().get());
  check(texture && texture->levels.front().pixels.size() == std::size_t{4U} * 1024U * 1024U &&
        texture->levels.front().pixels.back() == std::byte{127});
}

// 用可控预算和回执固定渐进更新的基础契约，不依赖真实 GPU 或计时等待。
void delayed_single_texture_update() {
  engine::core::tasks::task_scheduler scheduler(
      {.workers = 0U, .mode = engine::core::tasks::execution_mode::cooperative});
  auto files = std::make_shared<memory_files>();
  files->text(
      "texture",
      R"({"format":"gneiss.texture","version":1,"source":"asset://image.ktx2","color_space":"srgb"})");
  files->pixel(std::byte{10});
  virtual_file_system vfs;
  check(vfs.mount("asset://", files) == GNEISS_SUCCESS);
  render_resource_service resources;
  resource_cache cache;
  render_asset_loader loader(vfs, cache, resources);
  bool budget_available{};
  bool acknowledged{};
  unsigned attempts{};
  unsigned accepted{};
  gneiss_result outcome = GNEISS_SUCCESS;
  texture_upload_backend::data retained;
  texture_upload_backend backend{
      .begin =
          [&](auto data, auto& sequence) {
            ++attempts;
            if (!budget_available) {
              return GNEISS_ERROR_NOT_READY;
            }
            check(retained.empty() && data.size() == 1U);
            retained = std::move(data);
            sequence = ++accepted;
            return GNEISS_SUCCESS;
          },
      .poll =
          [&](auto, auto& result) {
            if (!acknowledged) {
              return false;
            }
            retained.clear();
            result = outcome;
            return true;
          },
      .discard = [](auto, auto&) { return GNEISS_ERROR_INTERNAL; },
      .flush = [&] { acknowledged = true; },
  };
  texture_load_service service(scheduler, vfs, loader, std::move(backend), {},
                               std::make_shared<cpu_document_cache>());
  const auto advance = [&] {
    (void)scheduler.run_ready();
    service.advance();
  };
  const auto until = [&](auto predicate) {
    for (unsigned step = 0U; step < 1024U && !predicate(); ++step) {
      advance();
    }
    check(predicate());
  };
  const std::array<std::string, 1> uris{"asset://texture"};
  std::uint64_t request{};
  texture_load_completion completion;
  // 首次解析失败仍保留使用者持有的逻辑 RID，显式重试填充同一槽位。
  auto payload = std::move(files->files.at("image.ktx2"));
  files->files.erase("image.ktx2");
  check(service.submit(uris, 1U, 1U, request) == GNEISS_SUCCESS);
  auto first_target = service.requested_assets().front();
  bool failed{};
  until([&] { return failed || (failed = service.take(completion)); });
  check(completion.state == texture_load_state::failed && accepted == 0U &&
        resources.texture_status(first_target.get()) == GNEISS_ERROR_NOT_READY);
  render_asset_lease cached;
  check(loader.acquire_cached({uris.front(), render_asset_type::texture}, cached) ==
        GNEISS_ERROR_NOT_READY);
  files->files["image.ktx2"] = std::move(payload);
  check(service.submit(uris, 1U, 1U, request) == GNEISS_SUCCESS);
  check(service.requested_assets().size() == 1U);
  const auto reserved_rid = service.requested_assets().front().get();
  check(reserved_rid == first_target.get());
  first_target = {};
  check(resources.texture_status(reserved_rid) == GNEISS_ERROR_NOT_READY && cache.size() == 1U);
  until([&] { return attempts != 0U; });
  for (unsigned frame = 0U; frame < 8U; ++frame) {
    advance();
    check(accepted == 0U && cache.size() == 1U && !service.take(completion));
  }
  budget_available = true;
  until([&] { return accepted == 1U; });
  const auto submitted_attempts = attempts;
  for (unsigned frame = 0U; frame < 8U; ++frame) {
    advance();
    check(attempts == submitted_attempts && cache.size() == 1U && !retained.empty() &&
          resources.texture_status(reserved_rid) == GNEISS_ERROR_NOT_READY);
  }
  acknowledged = true;
  bool initial_complete{};
  until([&] { return initial_complete || (initial_complete = service.take(completion)); });
  check(completion.state == texture_load_state::applied && completion.assets.size() == 1U &&
        completion.assets.front().type() == gneiss::engine::render::render_asset_type::texture);
  auto lease = completion.assets.front();
  const auto rid = lease.get();
  check(rid == reserved_rid && resources.texture_status(rid) == GNEISS_SUCCESS);
  auto old_frame = resources.share_texture(rid);
  check(old_frame && old_frame->levels.front().pixels.front() == std::byte{10});

  // 相同 RID 的失败更新和成功更新，都不提前改变旧帧持有的数据。
  for (const auto expected : {GNEISS_ERROR_INITIALIZATION_FAILED, GNEISS_SUCCESS}) {
    files->pixel(std::byte{20});
    acknowledged = false;
    outcome = expected;
    const auto before = accepted;
    check(service.submit(uris, 1U, before + 1U, request) == GNEISS_SUCCESS);
    until([&] { return accepted == before + 1U; });
    const auto update_attempts = attempts;
    for (unsigned frame = 0U; frame < 8U; ++frame) {
      advance();
      check(attempts == update_attempts && resources.share_texture(rid) == old_frame &&
            !service.take(completion));
    }
    acknowledged = true;
    bool complete{};
    until([&] { return complete || (complete = service.take(completion)); });
    check(completion.result == expected && retained.empty());
    check(old_frame->levels.front().pixels.front() == std::byte{10});
    if (expected == GNEISS_SUCCESS) {
      check(completion.assets.front().get() == rid &&
            resources.share_texture(rid)->levels.front().pixels.front() == std::byte{20});
    } else {
      check(resources.share_texture(rid) == old_frame);
      for (unsigned frame = 0U; frame < 8U; ++frame) {
        advance();
      }
      check(attempts == update_attempts);
    }
  }
  completion = {};
  lease = {};
  cache.release_unused();
  check(!resources.share_texture(rid) && resources.live_resource_count() == 0U);
  check(old_frame->levels.front().pixels.front() == std::byte{10});
  service.request_stop();
  check(service.stopped());
}
void run(engine::core::tasks::execution_mode mode) {
  engine::core::tasks::task_scheduler scheduler({.workers = 2U, .mode = mode});
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
  texture_load_service service(scheduler, vfs, loader, std::move(backend), {},
                               std::make_shared<cpu_document_cache>());
  const auto advance = [&] {
    if (mode == engine::core::tasks::execution_mode::cooperative) {
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
  check(cache.size() == 1U && resources.texture_status(service.requested_assets().front().get()) ==
                                  GNEISS_ERROR_NOT_READY);
  check((files->reader == std::this_thread::get_id()) ==
        (mode == engine::core::tasks::execution_mode::cooperative));
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
  check(completion.state == texture_load_state::applied && completion.assets.size() == 1U &&
        completion.assets.front().type() == gneiss::engine::render::render_asset_type::texture);
  auto lease = completion.assets.front();
  const auto rid = lease.get();
  auto previous = resources.share_texture(rid);
  check(previous && previous->levels.front().pixels.front() == std::byte{10});
  files->reader = {};
  check(service.submit(uris, 1U, 2U, request, false) == GNEISS_SUCCESS);
  check(service.take(completion) && completion.assets.front().get() == rid &&
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
  check(completion.state == texture_load_state::applied && completion.assets.front().get() == rid &&
        resources.share_texture(rid) != previous &&
        previous->levels.front().pixels.front() == std::byte{10});
  previous = resources.share_texture(rid);
  const std::vector<std::string> broken{uris.front(), "asset://b.texture.json"};
  check(service.submit(broken, 1U, 4U, request) == GNEISS_SUCCESS);
  until([&] { return service.take(completion); });
  check(completion.state == texture_load_state::failed &&
        resources.share_texture(rid) != previous && cache.size() == 1U &&
        completion.assets.size() == 1U && completion.assets.front().get() == rid &&
        completion.results[0].applied && !completion.results[1].applied);
  previous = resources.share_texture(rid);
  // 同步重载保持 RID，但更新内容与修订号；迟到的 GPU 候选不能覆盖它。
  uploaded = false;
  ready = false;
  check(service.submit(uris, 1U, 5U, request) == GNEISS_SUCCESS);
  until([&] { return uploaded; });
  asset_diagnostic diagnostic;
  const std::vector<render_asset_reload> synchronous{{uris.front(), render_asset_type::texture}};
  files->pixel(std::byte{30});
  check(loader.reload_assets(synchronous, diagnostic) == GNEISS_SUCCESS);
  render_asset_lease newer;
  check(loader.acquire_cached(synchronous.front(), newer) == GNEISS_SUCCESS);
  const auto newer_rid = newer.get();
  const auto newer_data = resources.share_texture(newer_rid);
  check(newer_rid == rid && newer_data != previous &&
        newer_data->levels.front().pixels.front() == std::byte{30});
  ready = true;
  until([&] { return service.take(completion); });
  check(completion.state == texture_load_state::failed && discarded == 1U &&
        resources.share_texture(rid) == newer_data &&
        previous->levels.front().pixels.front() == std::byte{20});
  lease = std::move(newer);
  // 请求持有目标租约；显式销毁后仍不能被迟到候选复活。
  check(service.submit(uris, 1U, 5U, request) == GNEISS_SUCCESS);
  check(resources.destroy_texture(newer_rid) == GNEISS_SUCCESS);
  lease = {};
  completion = {};
  cache.release_unused();
  check(resources.get_texture(newer_rid) == nullptr);
  until([&] { return service.take(completion); });
  cache.release_unused();
  check(completion.state == texture_load_state::failed && cache.size() == 0U);
  check(service.submit(uris, 2U, 1U, request) == GNEISS_SUCCESS);
  service.cancel();
  until([&] { return service.take(completion); });
  check(completion.state == texture_load_state::cancelled && resources.live_resource_count() == 0U);
  service.request_stop();
  check(service.stopped() && discarded == 1U);
}
void mixed(engine::core::tasks::execution_mode mode, bool pbr = false) {
  engine::core::tasks::task_scheduler scheduler({.workers = 1U, .mode = mode});
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
  std::size_t first_chunk{};
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
            if (chunks == 1U) {
              first_chunk = data.size();
            }
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
            check(data.size() == expected_assets - first_chunk);
            ++discards;
            sequence = ++serial;
            return GNEISS_SUCCESS;
          },
      .flush = [&] { ack = true; },
      .estimate_bytes = [](const auto& item) -> std::size_t {
        return item.mesh ? 5U * 1024U * 1024U : 1U * 1024U * 1024U;
      }};
  texture_load_service service(scheduler, vfs, loader, std::move(backend), {},
                               std::make_shared<cpu_document_cache>());
  const auto advance = [&] {
    if (mode == engine::core::tasks::execution_mode::cooperative) {
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
  check(completion.state == texture_load_state::failed && discards == 0U &&
        chunks == expected_assets && completion.assets.size() == expected_assets - 1U);
  check(std::ranges::count_if(completion.results, [](const auto& item) { return item.applied; }) ==
        expected_assets - 1U);
  check(std::ranges::count_if(completion.results, [](const auto& item) {
          return !item.applied && item.result == GNEISS_ERROR_IO;
        }) == 1U);
  completion = {};
  cache.release_unused();
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
  render_asset_lease unchanged_lease;
  check(loader.acquire_cached(requested[2], unchanged_lease) == GNEISS_SUCCESS);
  const auto unchanged_mesh = resources.share_mesh(unchanged_lease.get());
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
  // 真实异步重载只改变第一个网格，其他网格继续复用同一 CPU 对象。
  check(resources.share_mesh(unchanged_lease.get()) == unchanged_mesh);
  unchanged_lease = {};
  completion = {};
  leases.clear();
  cache.release_unused();
  const auto* material = resources.get_material(material_lease.get());
  check(material && resources.get_texture(material->base_color_texture) != nullptr);
  if (pbr) {
    check(material->emissive[2] == 0.4F);
    for (const auto texture : material->texture_handles()) {
      check(texture != gneiss::engine::core::null_rid && resources.get_texture(texture) != nullptr);
    }
    // 材质重载复用已成功的纹理；文件丢失不会隐式触发纹理重载。
    const auto retained = resources.share_material(material_lease.get());
    files->files.erase("3.texture.json");
    check(service.submit_assets(requested, 1U, 4U, request) == GNEISS_SUCCESS);
    until([&] { return service.take(completion); });
    check(completion.state == texture_load_state::applied &&
          resources.share_material(material_lease.get()) != retained);
    check(completion.assets.size() == expected_assets &&
          std::ranges::count_if(completion.results, [](const auto& item) {
            return item.applied && item.source.type == render_asset_type::mesh;
          }) == 5U);
    // 显式重载丢失纹理只使该项失败；材质和网格成功更新，旧纹理仍可用。
    auto explicit_texture = requested;
    explicit_texture.push_back({"asset://3.texture.json", render_asset_type::texture});
    const auto texture_rid = resources.get_material(material_lease.get())->occlusion_texture;
    const auto old_texture = resources.share_texture(texture_rid);
    check(service.submit_assets(explicit_texture, 1U, 5U, request) == GNEISS_SUCCESS);
    until([&] { return service.take(completion); });
    check(completion.state == texture_load_state::failed &&
          completion.assets.size() == expected_assets - 1U &&
          resources.share_texture(texture_rid) == old_texture);
    const auto failure = std::ranges::find_if(completion.results, [](const auto& item) {
      return item.source.uri == "asset://3.texture.json";
    });
    check(failure != completion.results.end() && !failure->applied &&
          failure->result == GNEISS_ERROR_NOT_FOUND && completion.results.front().applied);
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
void packaged_lifetime(engine::core::tasks::execution_mode mode) {
#if defined(GNEISS_HAS_GRANIT_PLATFORM)
  engine::core::tasks::task_scheduler scheduler({.workers = 1U, .mode = mode});
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
  auto service =
      std::make_unique<texture_load_service>(scheduler, vfs, loader, std::move(backend),
                                             std::shared_ptr<engine::core::progress_notification>{},
                                             std::make_shared<cpu_document_cache>());
  const auto until = [&](auto predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!predicate()) {
      check(std::chrono::steady_clock::now() < deadline);
      if (mode == engine::core::tasks::execution_mode::cooperative) {
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
void cancel_unpublished_upload(engine::core::tasks::execution_mode mode, bool after_first = false) {
  engine::core::tasks::task_scheduler scheduler({.workers = 1U, .mode = mode});
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
            check(data.size() == 1U);
            retained = std::move(data);
            sequence = 2U;
            ++discards;
            return GNEISS_SUCCESS;
          },
      .flush = [&] { upload_ack = discard_ack = true; },
      .estimate_bytes = [](const auto&) { return texture_load_service::upload_budget_bytes; },
  };
  texture_load_service service(scheduler, vfs, loader, std::move(backend), {},
                               std::make_shared<cpu_document_cache>());
  const auto advance = [&] {
    if (mode == engine::core::tasks::execution_mode::cooperative) {
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
  auto first = after_first ? service.requested_assets().front() : render_asset_lease{};
  until([&] { return uploads == 1U; });
  if (after_first) {
    upload_ack = true;
    until([&] { return uploads == 2U; });
    upload_ack = false;
    check(resources.texture_status(first.get()) == GNEISS_SUCCESS);
    check(!service.take(completion));
  }
  const auto submitted = uploads;
  check(!service.cancel() && service.cancel_unpublished() && service.cancel_unpublished());
  advance();
  check(!service.take(completion) && uploads == submitted && discards == 0U && !retained.empty());
  upload_ack = true;
  until([&] { return discards == 1U; });
  advance();
  check(!service.take(completion) && uploads == submitted && cache.size() == 2U &&
        !retained.empty());
  discard_ack = true;
  until([&] { return service.take(completion); });
  check(completion.state == texture_load_state::cancelled && retained.empty() &&
        resources.live_resource_count() == (after_first ? 1U : 0U) &&
        !service.cancel_unpublished());
  if (after_first) {
    check(resources.texture_status(first.get()) == GNEISS_SUCCESS && cache.size() == 1U);
    check(completion.results.size() == 2U && completion.results.front().applied &&
          completion.results.front().result == GNEISS_SUCCESS &&
          !completion.results.back().applied &&
          completion.results.back().result == GNEISS_ERROR_INVALID_STATE);
  }
  check(service.submit(uris, 1U, 2U, request, false) == GNEISS_SUCCESS);
  until([&] { return service.take(completion); });
  check(completion.state == texture_load_state::applied &&
        uploads == submitted + (after_first ? 1U : 2U) && discards == 1U && cache.size() == 2U);
}
} // namespace

int main() try {
  independent_update_sequences();
  reserved_material_lifetime();
  delayed_single_texture_update();
  shared_mesh_documents();
  prepared_source_revisions();
  cooperative_read_slices();
  cancel_unpublished_upload(engine::core::tasks::execution_mode::cooperative);
  cancel_unpublished_upload(engine::core::tasks::execution_mode::thread_pool);
  cancel_unpublished_upload(engine::core::tasks::execution_mode::cooperative, true);
  cancel_unpublished_upload(engine::core::tasks::execution_mode::thread_pool, true);
  packaged_lifetime(engine::core::tasks::execution_mode::cooperative);
  packaged_lifetime(engine::core::tasks::execution_mode::thread_pool);
  mixed(engine::core::tasks::execution_mode::cooperative, true);
  mixed(engine::core::tasks::execution_mode::thread_pool, true);
  mixed(engine::core::tasks::execution_mode::cooperative);
  mixed(engine::core::tasks::execution_mode::thread_pool);
  run(engine::core::tasks::execution_mode::cooperative);
  run(engine::core::tasks::execution_mode::thread_pool);
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
