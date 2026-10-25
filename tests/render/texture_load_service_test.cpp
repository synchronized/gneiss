// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "engine/function/render/render_resource_service.hpp"
#include "engine/function/render/texture_load_service.hpp"
#include "engine/resource/loaders/texture_container.hpp"
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
using namespace gneiss::engine;
void check(bool value, std::source_location at = std::source_location::current()) {
  if (!value) {
    throw std::runtime_error("异步资产契约失败，行=" + std::to_string(at.line()));
  }
}
struct memory_files final : file_system {
  std::map<std::string, std::vector<std::byte>> files;
  mutable std::thread::id reader;
  std::shared_ptr<std::atomic_size_t> bytes_read = std::make_shared<std::atomic_size_t>();
  gneiss::engine::result open_read(std::string_view path,
                                   std::unique_ptr<read_source>& output) const noexcept override {
    reader = std::this_thread::get_id();
    output.reset();
    const auto found = files.find(std::string(path));
    if (found == files.end()) {
      return gneiss::engine::result::not_found;
    }
    class memory_source final : public read_source {
    public:
      explicit memory_source(std::vector<std::byte> bytes,
                             std::shared_ptr<std::atomic_size_t> bytes_read)
          : bytes_(std::move(bytes)), bytes_read_(std::move(bytes_read)) {}
      std::uint64_t size() const noexcept override { return bytes_.size(); }
      gneiss::engine::result read_at(std::uint64_t offset,
                                     std::span<std::byte> output) const noexcept override {
        if (offset > bytes_.size() || output.size() > bytes_.size() - offset) {
          return gneiss::engine::result::io;
        }
        std::ranges::copy(
            std::span{bytes_}.subspan(static_cast<std::size_t>(offset), output.size()),
            output.begin());
        bytes_read_->fetch_add(output.size(), std::memory_order_relaxed);
        return gneiss::engine::result::success;
      }

    private:
      std::vector<std::byte> bytes_;
      std::shared_ptr<std::atomic_size_t> bytes_read_;
    };
    try {
      output = std::make_unique<memory_source>(found->second, bytes_read);
    } catch (...) {
      return gneiss::engine::result::out_of_memory;
    }
    return gneiss::engine::result::success;
  }
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
  virtual_file_system files;
  check(files.mount("asset://", source) == gneiss::engine::result::success);
  const std::array requests{asset_preparation_source{"asset://material", asset_type::material}};
  prepared_batch before;
  prepared_batch after;
  asset_diagnostic diagnostic;
  check(prepare_assets(files, requests, before, diagnostic, {}, 128U) ==
        gneiss::engine::result::success);
  prepared_batch repeated;
  check(prepare_assets(files, requests, repeated, diagnostic, {}, 128U) ==
        gneiss::engine::result::success);
  source->pixel(std::byte{20});
  source->files["payload/image.ktx2"] = source->files.at("image.ktx2");
  check(prepare_assets(files, requests, after, diagnostic, {}, 128U) ==
        gneiss::engine::result::success);
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
                           expected) == gneiss::engine::result::success);
      check(payload.identity == expected && payload.identity == old_asset.sources.back().identity);
      check(payload.bytes == source->files.at("image.ktx2").size());
      check(payload.digest == gneiss::engine::sha256(source->files.at("image.ktx2")));
      check(payload.digest != old_asset.sources.back().digest);
    } else {
      check(old_asset.sources.size() == 1U && new_asset.sources.size() == 1U);
      check(new_asset.texture_uris[0] == "asset://texture");
    }
  }
}
void mesh_data_lifetime() {
  auto source = std::make_shared<memory_files>();
  const std::string text =
      R"({"format":"gneiss.mesh","version":3,"topology":"triangle_list","vertices":[[0,0,0],[1,0,0],[0,1,0]],"uvs":[[0,0],[1,0],[0,1]],"normals":[[0,0,1],[0,0,1],[0,0,1]]})";
  source->text("mesh", text);
  virtual_file_system files;
  check(files.mount("asset://", source) == gneiss::engine::result::success);
  const std::array requests{asset_preparation_source{"asset://mesh", asset_type::mesh}};
  auto prepare = [&](const virtual_file_system& vfs) {
    prepared_batch batch;
    asset_diagnostic diagnostic;
    check(prepare_assets(vfs, requests, batch, diagnostic, {}, 128U) ==
          gneiss::engine::result::success);
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
  virtual_file_system files;
  check(files.mount("asset://", std::make_shared<memory_files>()) ==
        gneiss::engine::result::success);
  render_resource_service resources;
  resource_cache cache;
  render_asset_loader loader(files, cache, resources);
  const render_asset_reload source_a{"asset://a", render_asset_type::material};
  const render_asset_reload source_b{"asset://b", render_asset_type::material};
  render_asset_ref a, b;
  check(loader.reserve_asset(source_a, a) == gneiss::engine::result::success);
  check(loader.reserve_asset(source_b, b) == gneiss::engine::result::success);
  virtual_file_system different;
  check(different.mount("asset://", std::make_shared<memory_files>()) ==
        gneiss::engine::result::success);
  render_asset_loader isolated(different, cache, resources);
  render_asset_ref conflict;
  check(isolated.reserve_asset(source_a, conflict) == gneiss::engine::result::success &&
        conflict != a && gneiss::engine::asset_rid(conflict) != gneiss::engine::asset_rid(a));
  check(!isolated.owns(a));
  const auto copied = files;
  render_asset_loader shared(copied, cache, resources);
  check(shared.reserve_asset(source_a, conflict) == gneiss::engine::result::success &&
        gneiss::engine::asset_rid(conflict) == gneiss::engine::asset_rid(a));

  const auto stage = [&](const auto& source, const auto& target, auto sequence, auto& output) {
    prepared_render_asset prepared;
    prepared.source = source;
    return loader.prepare_upload(std::move(prepared), output, target, sequence);
  };
  std::array<render_asset_loader::asset_upload, 1> first, unrelated, late, latest;
  const auto first_sequence = loader.next_update_sequence();
  check(stage(source_a, a, first_sequence, first.front()) == gneiss::engine::result::success);
  check(stage(source_b, b, loader.next_update_sequence(), unrelated.front()) ==
        gneiss::engine::result::success);
  check(loader.complete_upload(unrelated.front()) == gneiss::engine::result::success);
  check(loader.complete_upload(first.front()) == gneiss::engine::result::success);
  const auto previous = resources.share_material(gneiss::engine::asset_rid(a));
  const auto old_sequence = loader.next_update_sequence();
  check(stage(source_a, a, old_sequence, late.front()) == gneiss::engine::result::success);
  const auto new_sequence = loader.next_update_sequence();
  check(loader.claim_update(a, new_sequence));
  // 新请求尚未完成也阻止旧任务覆盖；最新请求失败时仍保留当前成功内容。
  check(loader.complete_upload(late.front()) == gneiss::engine::result::invalid_state);
  check(resources.share_material(gneiss::engine::asset_rid(a)) == previous);
  check(stage(source_a, a, old_sequence, late.front()) == gneiss::engine::result::invalid_state);
  check(stage(source_a, a, new_sequence, latest.front()) == gneiss::engine::result::success);
  check(loader.complete_upload(latest.front()) == gneiss::engine::result::success);
}
void reserved_material_lifetime() {
  auto source = std::make_shared<memory_files>();
  virtual_file_system files;
  check(files.mount("asset://", source) == gneiss::engine::result::success);
  render_resource_service resources;
  resource_cache cache;
  render_asset_loader loader(files, cache, resources);
  const render_asset_reload request{"asset://material", render_asset_type::material};
  render_asset_ref target;
  check(loader.reserve_asset(request, target) == gneiss::engine::result::success);
  const auto rid = gneiss::engine::asset_rid(target);
  prepared_render_asset prepared;
  prepared.source = request;
  std::array<render_asset_loader::asset_upload, 1> candidates;
  check(loader.prepare_upload(std::move(prepared), candidates.front(), target) ==
        gneiss::engine::result::success);
  check(resources.material_status(rid) == gneiss::engine::result::not_ready);
  check(loader.complete_upload(candidates.front()) == gneiss::engine::result::success);
  candidates = {};
  check(resources.material_status(rid) == gneiss::engine::result::success);
  target = {};
  cache.release_unused();
  check(resources.material_status(rid) == gneiss::engine::result::invalid_handle);
}
void cooperative_read_slices() {
  gneiss::engine::task_scheduler scheduler({.mode = gneiss::engine::execution_mode::cooperative});
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
  check(vfs.mount("asset://", files) == gneiss::engine::result::success);
  render_resource_service resources;
  resource_cache cache;
  render_asset_loader loader(vfs, cache, resources);
  texture_upload_backend backend{
      .begin =
          [](const auto&, auto& sequence) {
            sequence = 1U;
            return gneiss::engine::result::success;
          },
      .poll =
          [](auto, auto& result) {
            result = gneiss::engine::result::success;
            return true;
          },
      .discard =
          [](const auto&, auto& sequence) {
            sequence = 2U;
            return gneiss::engine::result::success;
          },
      .flush = [] {},
  };
  texture_load_service service(scheduler, vfs, loader, std::move(backend), {});
  const std::vector<std::string> uris{"asset://large.texture.json"};
  std::uint64_t request{};
  check(service.submit(uris, 1U, 1U, request) == gneiss::engine::result::success);
  texture_load_completion completion;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  std::size_t reading_tasks{};
  while (!service.take(completion)) {
    check(std::chrono::steady_clock::now() < deadline);
    const auto before = files->bytes_read->load();
    (void)scheduler.run_ready({.max_tasks = 1U});
    const auto consumed = files->bytes_read->load() - before;
    // 验证服务真实消费量，而不是只检查预算常量；大载荷仍须完整分段读取。
    check(consumed <= std::size_t{4U} * 1024U * 1024U);
    reading_tasks += consumed != 0U ? 1U : 0U;
    service.advance();
  }
  // 一遍约 5.3 MiB 的载荷至少需要两块；相邻阶段允许共用剩余预算。
  if (reading_tasks < 2U || completion.state != texture_load_state::applied) {
    throw std::runtime_error("大纹理分片失败：读取任务=" + std::to_string(reading_tasks) +
                             "，结果=" + std::to_string(completion.result.code()) +
                             "，消息=" + completion.message);
  }
  check(files->bytes_read->load() ==
        files->files["image.ktx2"].size() + files->files["large.texture.json"].size());
  check(completion.assets.size() == 1U &&
        gneiss::engine::asset_kind(completion.assets.front()) ==
            gneiss::engine::render_asset_type::texture &&
        cache.size() == 1U);
  const auto texture =
      resources.share_texture(gneiss::engine::asset_rid(completion.assets.front()));
  check(texture && texture->levels.front().pixels.size() == std::size_t{4U} * 1024U * 1024U &&
        texture->levels.front().pixels.back() == std::byte{127});
}

// 用可控预算和回执固定渐进更新的基础契约，不依赖真实 GPU 或计时等待。
void delayed_single_texture_update() {
  gneiss::engine::task_scheduler scheduler(
      {.workers = 0U, .mode = gneiss::engine::execution_mode::cooperative});
  auto files = std::make_shared<memory_files>();
  files->text(
      "texture",
      R"({"format":"gneiss.texture","version":1,"source":"asset://image.ktx2","color_space":"srgb"})");
  files->pixel(std::byte{10});
  virtual_file_system vfs;
  check(vfs.mount("asset://", files) == gneiss::engine::result::success);
  render_resource_service resources;
  resource_cache cache;
  render_asset_loader loader(vfs, cache, resources);
  bool budget_available{};
  bool acknowledged{};
  unsigned attempts{};
  unsigned accepted{};
  gneiss::engine::result outcome = gneiss::engine::result::success;
  texture_upload_backend::data retained;
  texture_upload_backend backend{
      .begin =
          [&](auto data, auto& sequence) {
            ++attempts;
            if (!budget_available) {
              return gneiss::engine::result::not_ready;
            }
            check(retained.empty() && data.size() == 1U);
            retained = std::move(data);
            sequence = ++accepted;
            return gneiss::engine::result::success;
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
      .discard = [](auto, auto&) { return gneiss::engine::result::internal; },
      .flush = [&] { acknowledged = true; },
  };
  texture_load_service service(scheduler, vfs, loader, std::move(backend), {});
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
  check(service.submit(uris, 1U, 1U, request) == gneiss::engine::result::success);
  auto first_target = service.requested_assets().front();
  bool failed{};
  until([&] { return failed || (failed = service.take(completion)); });
  check(completion.state == texture_load_state::failed && accepted == 0U &&
        resources.texture_status(gneiss::engine::asset_rid(first_target)) ==
            gneiss::engine::result::not_ready);
  render_asset_ref cached;
  check(loader.acquire_cached({uris.front(), render_asset_type::texture}, cached) ==
        gneiss::engine::result::not_ready);
  files->files["image.ktx2"] = std::move(payload);
  check(service.submit(uris, 1U, 1U, request) == gneiss::engine::result::success);
  check(service.requested_assets().size() == 1U);
  const auto reserved_rid = gneiss::engine::asset_rid(service.requested_assets().front());
  check(reserved_rid == gneiss::engine::asset_rid(first_target));
  first_target = {};
  check(resources.texture_status(reserved_rid) == gneiss::engine::result::not_ready &&
        cache.size() == 1U);
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
          resources.texture_status(reserved_rid) == gneiss::engine::result::not_ready);
  }
  acknowledged = true;
  bool initial_complete{};
  until([&] { return initial_complete || (initial_complete = service.take(completion)); });
  check(completion.state == texture_load_state::applied && completion.assets.size() == 1U &&
        gneiss::engine::asset_kind(completion.assets.front()) ==
            gneiss::engine::render_asset_type::texture);
  auto lease = completion.assets.front();
  const auto rid = gneiss::engine::asset_rid(lease);
  check(rid == reserved_rid && resources.texture_status(rid) == gneiss::engine::result::success);
  auto old_frame = resources.share_texture(rid);
  check(old_frame && old_frame->levels.front().pixels.front() == std::byte{10});

  // 相同 RID 的失败更新和成功更新，都不提前改变旧帧持有的数据。
  for (const auto expected :
       {gneiss::engine::result::initialization_failed, gneiss::engine::result::success}) {
    files->pixel(std::byte{20});
    acknowledged = false;
    outcome = expected;
    const auto before = accepted;
    check(service.submit(uris, 1U, before + 1U, request) == gneiss::engine::result::success);
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
    if (expected == gneiss::engine::result::success) {
      check(gneiss::engine::asset_rid(completion.assets.front()) == rid &&
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
void run(gneiss::engine::execution_mode mode) {
  gneiss::engine::task_scheduler scheduler({.workers = 2U, .mode = mode});
  auto files = std::make_shared<memory_files>();
  files->text(
      "a.texture.json",
      R"({"format":"gneiss.texture","version":1,"source":"asset://image.ktx2","color_space":"srgb"})");
  files->text(
      "b.texture.json",
      R"({"format":"gneiss.texture","version":1,"source":"asset://missing.ktx2","color_space":"srgb"})");
  files->pixel(std::byte{10});
  virtual_file_system vfs;
  check(vfs.mount("asset://", files) == gneiss::engine::result::success);
  render_resource_service resources;
  resource_cache cache;
  render_asset_loader loader(vfs, cache, resources);
  bool uploaded{};
  bool ready{};
  gneiss::engine::result upload_result = gneiss::engine::result::success;
  unsigned discarded{};
  texture_upload_backend backend{.begin =
                                     [&](auto, auto& sequence) {
                                       uploaded = true;
                                       sequence = 1U;
                                       return gneiss::engine::result::success;
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
                                       return gneiss::engine::result::success;
                                     },
                                 .flush = [&] { ready = true; }};
  texture_load_service service(scheduler, vfs, loader, std::move(backend), {});
  const auto advance = [&] {
    if (mode == gneiss::engine::execution_mode::cooperative) {
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
  check(service.submit(uris, 1U, 1U, request) == gneiss::engine::result::success && request != 0U);
  std::uint64_t rejected{};
  check(service.submit(uris, 1U, 2U, rejected) == gneiss::engine::result::not_ready);
  until([&] { return uploaded; });
  check(cache.size() == 1U &&
        resources.texture_status(gneiss::engine::asset_rid(service.requested_assets().front())) ==
            gneiss::engine::result::not_ready);
  check((files->reader == std::this_thread::get_id()) ==
        (mode == gneiss::engine::execution_mode::cooperative));
  texture_load_completion completion;
  check(!service.take(completion));
  upload_result = gneiss::engine::result::initialization_failed;
  ready = true;
  until([&] { return service.take(completion); });
  check(completion.state == texture_load_state::failed && cache.size() == 0U &&
        resources.live_resource_count() == 0U);
  upload_result = gneiss::engine::result::success;
  check(service.submit(uris, 1U, 2U, request) == gneiss::engine::result::success);
  until([&] { return service.take(completion); });
  check(completion.state == texture_load_state::applied && completion.assets.size() == 1U &&
        gneiss::engine::asset_kind(completion.assets.front()) ==
            gneiss::engine::render_asset_type::texture);
  auto lease = completion.assets.front();
  const auto rid = gneiss::engine::asset_rid(lease);
  auto previous = resources.share_texture(rid);
  check(previous && previous->levels.front().pixels.front() == std::byte{10});
  files->reader = {};
  check(service.submit(uris, 1U, 2U, request, false) == gneiss::engine::result::success);
  check(service.take(completion) && gneiss::engine::asset_rid(completion.assets.front()) == rid &&
        files->reader == std::thread::id{});
  files->pixel(std::byte{20});
  uploaded = false;
  ready = false;
  check(service.submit(uris, 1U, 3U, request) == gneiss::engine::result::success);
  until([&] { return uploaded; });
  check(resources.share_texture(rid) == previous);
  service.cancel(); // 上传已获许可，迟到取消不撤销提交。
  ready = true;
  until([&] { return service.take(completion); });
  check(completion.state == texture_load_state::applied &&
        gneiss::engine::asset_rid(completion.assets.front()) == rid &&
        resources.share_texture(rid) != previous &&
        previous->levels.front().pixels.front() == std::byte{10});
  previous = resources.share_texture(rid);
  const std::vector<std::string> broken{uris.front(), "asset://b.texture.json"};
  check(service.submit(broken, 1U, 4U, request) == gneiss::engine::result::success);
  until([&] { return service.take(completion); });
  check(completion.state == texture_load_state::failed &&
        resources.share_texture(rid) != previous && cache.size() == 1U &&
        completion.assets.size() == 1U &&
        gneiss::engine::asset_rid(completion.assets.front()) == rid &&
        completion.results[0].applied && !completion.results[1].applied);
  previous = resources.share_texture(rid);
  // 同步重载保持 RID，但更新内容与修订号；迟到的 GPU 候选不能覆盖它。
  uploaded = false;
  ready = false;
  check(service.submit(uris, 1U, 5U, request) == gneiss::engine::result::success);
  until([&] { return uploaded; });
  asset_diagnostic diagnostic;
  const std::vector<render_asset_reload> synchronous{{uris.front(), render_asset_type::texture}};
  files->pixel(std::byte{30});
  check(loader.reload_assets(synchronous, diagnostic) == gneiss::engine::result::success);
  render_asset_ref newer;
  check(loader.acquire_cached(synchronous.front(), newer) == gneiss::engine::result::success);
  const auto newer_rid = gneiss::engine::asset_rid(newer);
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
  check(service.submit(uris, 1U, 5U, request) == gneiss::engine::result::success);
  check(resources.destroy_texture(newer_rid) == gneiss::engine::result::success);
  lease = {};
  completion = {};
  cache.release_unused();
  check(resources.get_texture(newer_rid) == nullptr);
  until([&] { return service.take(completion); });
  cache.release_unused();
  check(completion.state == texture_load_state::failed && cache.size() == 0U);
  check(service.submit(uris, 2U, 1U, request) == gneiss::engine::result::success);
  service.cancel();
  until([&] { return service.take(completion); });
  check(completion.state == texture_load_state::cancelled && resources.live_resource_count() == 0U);
  service.request_stop();
  check(service.stopped() && discarded == 1U);
}
void mixed(gneiss::engine::execution_mode mode, bool pbr = false) {
  gneiss::engine::task_scheduler scheduler({.workers = 1U, .mode = mode});
  auto files = std::make_shared<memory_files>();
  files->pixel(std::byte{10});
  files->text(
      "a.texture.json",
      R"({"format":"gneiss.texture","version":1,"source":"asset://image.ktx2","color_space":"srgb"})");
  files->text(
      "m.material.json",
      R"({"format":"gneiss.material","version":5,"color":[1,1,1,1],"base_color_texture":"asset://a.texture.json","metallic":0,"roughness":1})");
  if (pbr) {
    for (unsigned i = 1; i < 5U; ++i) {
      files->text(
          std::to_string(i) + ".texture.json",
          R"({"format":"gneiss.texture","version":1,"source":"asset://image.ktx2","color_space":"srgb"})");
    }
    files->text(
        "m.material.json",
        R"({"format":"gneiss.material","version":5,"color":[1,1,1,1],"base_color_texture":"asset://a.texture.json","metallic":0,"roughness":1,"metallic_roughness_texture":"asset://1.texture.json","normal_texture":"asset://2.texture.json","occlusion_texture":"asset://3.texture.json","emissive_texture":"asset://4.texture.json","emissive":[0.2,0.3,0.4]})");
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
  check(vfs.mount("asset://", files) == gneiss::engine::result::success);
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
            return gneiss::engine::result::success;
          },
      .poll =
          [&](auto, auto& result) {
            result = fail && chunks == 2U && discards == 0U ? gneiss::engine::result::io
                                                            : gneiss::engine::result::success;
            return ack;
          },
      .discard =
          [&](auto data, auto& sequence) {
            check(data.size() == expected_assets - first_chunk);
            ++discards;
            sequence = ++serial;
            return gneiss::engine::result::success;
          },
      .flush = [&] { ack = true; },
      .estimate_bytes = [](const auto& item) -> std::size_t {
        return item.mesh ? 5U * 1024U * 1024U : 1U * 1024U * 1024U;
      }};
  texture_load_service service(scheduler, vfs, loader, std::move(backend), {});
  const auto advance = [&] {
    if (mode == gneiss::engine::execution_mode::cooperative) {
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
        gneiss::engine::result::invalid_argument);
  check(service.submit_assets(requested, 1U, 1U, request, false, 8U) ==
        gneiss::engine::result::success);
  until([&] { return service.take(completion); });
  check(completion.state == texture_load_state::failed && chunks == 0U && cache.size() == 0U &&
        resources.live_resource_count() == 0U);
  check(service.submit_assets(requested, 1U, 1U, request) == gneiss::engine::result::success);
  until([&] { return service.take(completion); });
  check(completion.state == texture_load_state::failed && discards == 0U &&
        chunks == expected_assets && completion.assets.size() == expected_assets - 1U);
  check(std::ranges::count_if(completion.results, [](const auto& item) { return item.applied; }) ==
        expected_assets - 1U);
  check(std::ranges::count_if(completion.results, [](const auto& item) {
          return !item.applied && item.result == gneiss::engine::result::io;
        }) == 1U);
  completion = {};
  cache.release_unused();
  fail = false;
  check(service.submit_assets(requested, 1U, 2U, request) == gneiss::engine::result::success);
  check(service.cancel());
  until([&] { return service.take(completion); });
  check(completion.state == texture_load_state::cancelled && cache.size() == 0U);

  check(service.submit_assets(requested, 1U, 2U, request) == gneiss::engine::result::success);
  until([&] { return service.take(completion); });
  check(completion.state == texture_load_state::applied &&
        completion.assets.size() == expected_assets);
  auto leases = completion.assets;
  render_asset_ref mesh_lease, material_lease;
  check(loader.acquire_cached(requested[1], mesh_lease) == gneiss::engine::result::success);
  check(loader.acquire_cached(requested[0], material_lease) == gneiss::engine::result::success);
  const auto old_mesh = resources.share_mesh(gneiss::engine::asset_rid(mesh_lease));
  render_asset_ref unchanged_lease;
  check(loader.acquire_cached(requested[2], unchanged_lease) == gneiss::engine::result::success);
  const auto unchanged_mesh = resources.share_mesh(gneiss::engine::asset_rid(unchanged_lease));
  const auto old_material = resources.share_material(gneiss::engine::asset_rid(material_lease));
  auto changed = mesh;
  changed.replace(changed.find("[1,0,0]"), 7U, "[2,0,0]");
  files->text("0.mesh.json", changed);
  files->pixel(std::byte{200});
  uploaded = false;
  ack = false;
  check(service.submit_assets(requested, 1U, 3U, request) == gneiss::engine::result::success);
  until([&] { return uploaded; });
  asset_load_progress progress;
  check(service.progress(progress) && !progress.can_cancel &&
        progress.total_assets == expected_assets && !service.cancel());
  check(resources.share_mesh(gneiss::engine::asset_rid(mesh_lease)) == old_mesh &&
        resources.share_material(gneiss::engine::asset_rid(material_lease)) == old_material);
  ack = true;
  until([&] { return service.take(completion); });
  check(completion.state == texture_load_state::applied &&
        resources.share_mesh(gneiss::engine::asset_rid(mesh_lease)) != old_mesh &&
        old_mesh->vertices[1].x == 1.0F &&
        resources.get_mesh(gneiss::engine::asset_rid(mesh_lease))->vertices[1].x == 2.0F);
  // 真实异步重载只改变第一个网格，其他网格继续复用同一 CPU 对象。
  check(resources.share_mesh(gneiss::engine::asset_rid(unchanged_lease)) == unchanged_mesh);
  unchanged_lease = {};
  completion = {};
  leases.clear();
  cache.release_unused();
  const auto* material = resources.get_material(gneiss::engine::asset_rid(material_lease));
  check(material && resources.get_texture(material->base_color_texture) != nullptr);
  if (pbr) {
    check(material->emissive[2] == 0.4F);
    for (const auto texture : material->texture_handles()) {
      check(texture != gneiss::engine::null_rid && resources.get_texture(texture) != nullptr);
    }
    // 材质重载复用已成功的纹理；文件丢失不会隐式触发纹理重载。
    const auto retained = resources.share_material(gneiss::engine::asset_rid(material_lease));
    files->files.erase("3.texture.json");
    check(service.submit_assets(requested, 1U, 4U, request) == gneiss::engine::result::success);
    until([&] { return service.take(completion); });
    check(completion.state == texture_load_state::applied &&
          resources.share_material(gneiss::engine::asset_rid(material_lease)) != retained);
    check(completion.assets.size() == expected_assets &&
          std::ranges::count_if(completion.results, [](const auto& item) {
            return item.applied && item.source.type == render_asset_type::mesh;
          }) == 5U);
    // 显式重载丢失纹理只使该项失败；材质和网格成功更新，旧纹理仍可用。
    auto explicit_texture = requested;
    explicit_texture.push_back({"asset://3.texture.json", render_asset_type::texture});
    const auto texture_rid =
        resources.get_material(gneiss::engine::asset_rid(material_lease))->occlusion_texture;
    const auto old_texture = resources.share_texture(texture_rid);
    check(service.submit_assets(explicit_texture, 1U, 5U, request) ==
          gneiss::engine::result::success);
    until([&] { return service.take(completion); });
    check(completion.state == texture_load_state::failed &&
          completion.assets.size() == expected_assets - 1U &&
          resources.share_texture(texture_rid) == old_texture);
    const auto failure = std::ranges::find_if(completion.results, [](const auto& item) {
      return item.source.uri == "asset://3.texture.json";
    });
    check(failure != completion.results.end() && !failure->applied &&
          failure->result == gneiss::engine::result::not_found &&
          completion.results.front().applied);
  }
  // 同 URI 类型冲突、依赖缺失、闭包容量限制都必须在发布前拒绝。
  prepared_render_batch prepared;
  asset_diagnostic diagnostic;
  auto conflicting = requested;
  conflicting.push_back({"asset://a.texture.json", render_asset_type::mesh});
  check(prepare_render_assets(vfs, conflicting, prepared, diagnostic, [] { return false; }) !=
        gneiss::engine::result::success);
  check(prepare_render_assets(
            vfs, requested, prepared, diagnostic, [] { return false; }, 2U) !=
        gneiss::engine::result::success);
  check(prepare_render_assets(vfs, requested, prepared, diagnostic, [] { return true; }) !=
        gneiss::engine::result::success);
  check(prepare_render_assets(
            vfs, requested, prepared, diagnostic, [] { return false; }, 256U, 8U) !=
        gneiss::engine::result::success);
  files->files.erase("image.ktx2");
  check(prepare_render_assets(vfs, requested, prepared, diagnostic, [] { return false; }) !=
        gneiss::engine::result::success);
}
void packaged_lifetime(gneiss::engine::execution_mode mode) {
#if defined(GNEISS_HAS_GRANIT_PLATFORM)
  gneiss::engine::task_scheduler scheduler({.workers = 1U, .mode = mode});
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
  check(vfs.mount("asset://", files) == gneiss::engine::result::success);
  render_resource_service resources;
  resource_cache cache;
  render_asset_loader loader(vfs, cache, resources);
  texture_upload_backend::data in_flight;
  std::weak_ptr<const std::vector<std::byte>> observed;
  bool ack{};
  bool oversized{true};
  auto upload_result = gneiss::engine::result::initialization_failed;
  texture_upload_backend backend{
      .begin =
          [&](auto data, auto& sequence) {
            check(data.size() == 1U && data.front().texture_payload &&
                  data.front().texture->payload.empty());
            observed = data.front().texture_payload;
            in_flight = std::move(data);
            sequence = 1U;
            return gneiss::engine::result::success;
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
            return gneiss::engine::result::success;
          },
      .flush = [&] { ack = true; },
      .estimate_bytes =
          [&](const auto&) {
            return oversized ? texture_load_service::maximum_upload_bytes + 1U : payload.size();
          },
      .profile = {.generation = 1U, .sampled_transfer_formats = {true, true, false, false}}};
  auto service = std::make_unique<texture_load_service>(
      scheduler, vfs, loader, std::move(backend),
      std::shared_ptr<gneiss::engine::progress_notification>{});
  const auto until = [&](auto predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!predicate()) {
      check(std::chrono::steady_clock::now() < deadline);
      if (mode == gneiss::engine::execution_mode::cooperative) {
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
  check(service->submit(uris, 1U, 1U, request) == gneiss::engine::result::success);
  until([&] { return service->take(completion); });
  check(completion.result == gneiss::engine::result::out_of_memory && in_flight.empty() &&
        observed.expired() && cache.size() == 0U && !completion.message.empty());
  oversized = false;
  // 失败回执前后分别验证强租约存活与销毁，并确认可重试。
  check(service->submit(uris, 1U, 1U, request) == gneiss::engine::result::success);
  until([&] { return !in_flight.empty(); });
  check(!observed.expired() && !service->cancel());
  asset_load_progress progress;
  check(service->progress(progress) && progress.upload_reserved_bytes == payload.size());
  ack = true;
  until([&] { return service->take(completion); });
  check(completion.state == texture_load_state::failed && observed.expired() && cache.size() == 0U);
  upload_result = gneiss::engine::result::success;
  ack = false;
  check(service->submit(uris, 1U, 2U, request) == gneiss::engine::result::success);
  until([&] { return !in_flight.empty(); });
  const auto old_frame = in_flight.front().texture;
  ack = true;
  until([&] { return service->take(completion); });
  check(completion.state == texture_load_state::applied && observed.expired() &&
        completion.peak_upload_bytes == payload.size());
  check(!service->progress(progress) && progress.upload_reserved_bytes == 0U);
  check(old_frame->payload.empty() && old_frame->upload_payload.expired());
  std::vector<std::byte> restored;
  check(old_frame->payload_source->read(restored, payload.size()) ==
            gneiss::engine::result::success &&
        std::ranges::equal(restored, payload));
  // 上传中关闭必须先等待回执；旧帧只保留元数据，不阻止负载租约销毁。
  ack = false;
  check(service->submit(uris, 1U, 3U, request) == gneiss::engine::result::success);
  until([&] { return !in_flight.empty(); });
  service.reset();
  check(in_flight.empty() && observed.expired());
#else
  (void)mode;
#endif
}
}
namespace {
void cancel_unpublished_upload(gneiss::engine::execution_mode mode, bool after_first = false) {
  gneiss::engine::task_scheduler scheduler({.workers = 1U, .mode = mode});
  auto files = std::make_shared<memory_files>();
  files->pixel(std::byte{10});
  for (const auto* path : {"a.texture.json", "b.texture.json"}) {
    files->text(
        path,
        R"({"format":"gneiss.texture","version":1,"source":"asset://image.ktx2","color_space":"srgb"})");
  }
  virtual_file_system vfs;
  check(vfs.mount("asset://", files) == gneiss::engine::result::success);
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
            return gneiss::engine::result::success;
          },
      .poll =
          [&](auto sequence, auto& result) {
            const bool ready = sequence == 1U ? upload_ack : discard_ack;
            if (ready) {
              retained.clear();
              result = gneiss::engine::result::success;
            }
            return ready;
          },
      .discard =
          [&](auto data, auto& sequence) {
            check(data.size() == 1U);
            retained = std::move(data);
            sequence = 2U;
            ++discards;
            return gneiss::engine::result::success;
          },
      .flush = [&] { upload_ack = discard_ack = true; },
      .estimate_bytes = [](const auto&) { return texture_load_service::upload_budget_bytes; },
  };
  texture_load_service service(scheduler, vfs, loader, std::move(backend), {});
  const auto advance = [&] {
    if (mode == gneiss::engine::execution_mode::cooperative) {
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
  check(service.submit(uris, 1U, 1U, request, false) == gneiss::engine::result::success);
  auto first = after_first ? service.requested_assets().front() : render_asset_ref{};
  until([&] { return uploads == 1U; });
  if (after_first) {
    upload_ack = true;
    until([&] { return uploads == 2U; });
    upload_ack = false;
    check(resources.texture_status(gneiss::engine::asset_rid(first)) ==
          gneiss::engine::result::success);
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
    check(resources.texture_status(gneiss::engine::asset_rid(first)) ==
              gneiss::engine::result::success &&
          cache.size() == 1U);
    check(completion.results.size() == 2U && completion.results.front().applied &&
          completion.results.front().result == gneiss::engine::result::success &&
          !completion.results.back().applied &&
          completion.results.back().result == gneiss::engine::result::invalid_state);
  }
  check(service.submit(uris, 1U, 2U, request, false) == gneiss::engine::result::success);
  until([&] { return service.take(completion); });
  check(completion.state == texture_load_state::applied &&
        uploads == submitted + (after_first ? 1U : 2U) && discards == 1U && cache.size() == 2U);
}
} // namespace

int main() try {
  mesh_data_lifetime();
  independent_update_sequences();
  reserved_material_lifetime();
  delayed_single_texture_update();
  prepared_source_revisions();
  cooperative_read_slices();
  cancel_unpublished_upload(gneiss::engine::execution_mode::cooperative);
  cancel_unpublished_upload(gneiss::engine::execution_mode::thread_pool);
  cancel_unpublished_upload(gneiss::engine::execution_mode::cooperative, true);
  cancel_unpublished_upload(gneiss::engine::execution_mode::thread_pool, true);
  packaged_lifetime(gneiss::engine::execution_mode::cooperative);
  packaged_lifetime(gneiss::engine::execution_mode::thread_pool);
  mixed(gneiss::engine::execution_mode::cooperative, true);
  mixed(gneiss::engine::execution_mode::thread_pool, true);
  mixed(gneiss::engine::execution_mode::cooperative);
  mixed(gneiss::engine::execution_mode::thread_pool);
  run(gneiss::engine::execution_mode::cooperative);
  run(gneiss::engine::execution_mode::thread_pool);
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
