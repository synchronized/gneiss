// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/resource/caors_material_document.hpp"
#include "engine/resource/caors_material_versions.hpp"
#include "engine/resource/caors_vfs_bridge.hpp"
#include "engine/resource/cpu_document_cache.hpp"
#include "engine/resource/native_file_system.hpp"
#include "engine/core/tasks/task_scheduler.hpp"
#include "uv_async_file.hpp"
#include "uv_loop_executor.hpp"
#include "uv_path_resolver.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <thread>
#include <type_traits>

namespace {
void check(bool ok, const char* message) {
  if (!ok)
    throw std::runtime_error(message);
}
void run(std::size_t document_limit) {
  using namespace gneiss::asset_internal;
  using namespace std::chrono_literals;
  using gneiss::result;
  const auto root = std::filesystem::temp_directory_path() /
                    ("gneiss-material-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  const auto path = root / "material.json";
  struct fixture_cleanup {
    std::filesystem::path path, root;
    ~fixture_cleanup() {
      std::error_code ec;
      std::filesystem::remove(path, ec);
      std::filesystem::remove(root, ec);
    }
  } fixture{path, root};
  auto write = [&](std::string_view text) {
    std::ofstream out(path, std::ios::binary);
    out << text;
    check(out.good(), "write fixture");
  };
  std::string initial = R"({"format":"gneiss.material","version":1,"color":[1,0,0,1]})";
  initial.resize(document_limit, ' ');
  write(initial);
  gneiss::io_internal::uv_loop_executor loop;
  check(loop.start() == result::success, "loop");
  auto native = std::make_shared<native_file_system>();
  check(native->initialize(root.generic_string()) == GNEISS_SUCCESS, "native root");
  native->set_async_io(std::make_shared<gneiss::io_internal::uv_file_io>(loop),
                       std::make_shared<gneiss::io_internal::uv_path_resolver>(loop));
  virtual_file_system files;
  check(files.mount("asset://", native) == GNEISS_SUCCESS, "mount");
  gneiss::tasks::task_scheduler scheduler({.workers = 1});
  auto context = gneiss::caors_internal::make_worker(scheduler);
  caors::tasks::external_executor worker(context);
  caors::tasks::execution_context completions;
  auto bridge = make_caors_vfs_bridge(files, worker.get_executor(), document_limit);
  bool rejected_zero_bridge = false;
  try {
    (void)make_caors_vfs_bridge(files, worker.get_executor(), 0);
  } catch (const std::invalid_argument&) {
    rejected_zero_bridge = true;
  }
  check(rejected_zero_bridge, "zero bridge budget rejected");
  bool rejected_zero_loader = false;
  try {
    (void)make_material_document_loader(*bridge, {}, 0);
  } catch (const std::invalid_argument&) {
    rejected_zero_loader = true;
  }
  check(rejected_zero_loader, "zero loader budget rejected");
  std::atomic_bool reject_verification{false};
  const auto host_thread = std::this_thread::get_id();
  auto loader = make_material_document_loader(
      *bridge,
      [files, host_thread, &reject_verification, document_limit](auto uri, auto bytes) {
        if (reject_verification.load())
          return result::invalid_state;
        if (std::this_thread::get_id() == host_thread)
          return result::internal;
        std::vector<std::byte> current;
        const result read{files.read_bounded(uri, document_limit, current)};
        if (read != result::success)
          return read;
        return gneiss::core::sha256(current) == gneiss::core::sha256(bytes) ? result::success
                                                                            : result::invalid_state;
      },
      document_limit);
  caors::resources::cache<material_document> cache;
  caors::assets::asset_manager manager({worker.get_executor(), completions.get_executor()});
  struct cleanup {
    caors::tasks::external_executor& worker;
    material_document_loader& loader;
    caors::tasks::execution_context& queue;
    ~cleanup() {
      worker.close();
      loader.abandon();
      (void)queue.drain();
    }
  } guard{worker, *loader, completions};
  check(!manager.register_type(cache, *loader, caors::string_name{"gneiss.material.document"}),
        "register");
  caors::guid::byte_array id_bytes{};
  id_bytes[0] = 1;
  const caors::resource_guid id{caors::guid::from_bytes(id_bytes)};
  caors::resources::file_source source(bridge);
  check(!manager.provider().bind(id, source, "asset://material.json"), "bind");
  auto wait = [&](auto predicate) {
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (!predicate()) {
      loader->pump();
      (void)completions.drain();
      context->collect();
      check(std::chrono::steady_clock::now() < deadline, "timeout");
      std::this_thread::sleep_for(1ms);
    }
  };
  auto first = manager.request_load<material_document>(id);
  auto second = manager.request_load<material_document>(id);
  wait([&] { return first.done() && second.done(); });
  check(first.loaded() && second.loaded(), "loaded");
  auto old = first.try_get();
  check(old.get() == second.try_get().get() && loader->started_count() == 1, "coalesced");
  check(old->source.parameters.color[0] == 1, "red parsed");
  write(R"({"format":"gneiss.material","version":1,"color":[0,1,0,1]})");
  auto reload = manager.request_reload<material_document>(id);
  wait([&] { return reload.done(); });
  check(reload.loaded(), "reload");
  auto current = reload.try_get();
  check(current.get() != old.get() && current->digest != old->digest, "distinct version");
  check(old->source.parameters.color[0] == 1 && current->source.parameters.color[1] == 1,
        "old version retained");
  // 来源回执来自同一个 VFS；索引只认确切版本，不用 GUID 推断来源。
  asset_identity identity;
  check(files.identify("asset://material.json", static_cast<std::uint32_t>(asset_type::material),
                       identity) == GNEISS_SUCCESS,
        "material identity");
  const prepared_source old_source{identity, old->source_bytes, old->digest};
  const prepared_source new_source{identity, current->source_bytes, current->digest};
  material_document_lease old_lease, new_lease, found;
  material_document_versions versions;
  check(versions.remember(old_source, old, old_lease) == result::success &&
            versions.remember(new_source, current, new_lease) == result::success,
        "remember versions");
  check(versions.find(old_source, found) == result::success && found.get() == old.get(),
        "find old exact version");
  check(versions.find(new_source, found) == result::success && found.get() == current.get(),
        "find new exact version");
  // 相同内容来自另一次加载，可复用已存活的解析结果。
  auto duplicate = caors::resources::make_handle<material_document>(nullptr, id);
  check(static_cast<bool>(duplicate), "duplicate allocation");
  duplicate->source = old->source;
  duplicate->digest = old->digest;
  duplicate->source_bytes = old->source_bytes;
  check(versions.remember(old_source, duplicate, found) == result::success &&
            found.get() == old.get(),
        "same version canonical document");
  auto other_source = old_source;
  other_source.identity.source = std::make_shared<asset_source_domain>();
  check(versions.find(other_source, found) == result::not_ready && !found,
        "same uri and bytes in other source cannot hit");
  check(versions.remember(other_source, duplicate, found) == result::success &&
            found.get() == duplicate.get(),
        "different source remains separate");
  material_document_lease detached;
  {
    material_document_versions temporary;
    check(temporary.remember(old_source, old, detached) == result::success, "temporary index");
  }
  check(detached.get() == old.get() && detached.source().identity == identity,
        "lease survives index destruction");
  static_assert(std::is_same_v<decltype(detached.get()), const material_document*>);
  check(versions.remember(old_source, {}, found) == result::invalid_argument && !found,
        "null document rejected");
  auto invalid_receipt = old_source;
  invalid_receipt.digest = current->digest;
  check(versions.remember(invalid_receipt, old, found) == result::invalid_argument && !found,
        "digest mismatch rejected");
  invalid_receipt = old_source;
  ++invalid_receipt.bytes;
  check(versions.remember(invalid_receipt, old, found) == result::invalid_argument,
        "size mismatch rejected");
  invalid_receipt = old_source;
  invalid_receipt.identity.type = static_cast<std::uint32_t>(asset_type::texture);
  check(versions.remember(invalid_receipt, old, found) == result::invalid_argument,
        "wrong type rejected");
  invalid_receipt = old_source;
  invalid_receipt.identity.source.reset();
  check(versions.remember(invalid_receipt, old, found) == result::invalid_argument,
        "missing source rejected");
  duplicate = {};
  check(versions.find(other_source, found) == result::not_ready,
        "weak index does not keep document alive");
  std::weak_ptr<const asset_source_domain> expired_source = other_source.identity.source;
  other_source = {};
  versions.collect();
  check(expired_source.expired(), "collect releases source marker");
  check(versions.find(old_source, found) == result::success, "collect preserves live old version");
  reject_verification = true;
  auto rejected = manager.request_reload<material_document>(id);
  wait([&] { return rejected.done(); });
  check(rejected.failed() && cache.find_handle(id).get() == current.get(),
        "verification failure preserves current");
  reject_verification = false;
  write("{}");
  auto invalid = manager.request_reload<material_document>(id);
  wait([&] { return invalid.done(); });
  check(invalid.failed() && cache.find_handle(id).get() == current.get(),
        "failure preserves current");
  auto cancelled = manager.request_reload<material_document>(id);
  check(cancelled.cancel(), "cancel");
  wait([&] { return !manager.database().has_active_operations() && loader->empty(); });
  check(cache.find_handle(id).get() == current.get(), "cancel preserves current");
  // 直接走生产使用的分步准备：复用已读取字节，不再从 Loader 启动第二次读取。
  auto shared_documents = make_cpu_document_cache();
  check(shared_documents != nullptr, "material cache enabled");
  auto prepare = [&](bool change_source, bool dependency_missing) {
    auto documents = shared_documents->begin();
    write(
        dependency_missing
            ? R"({"format":"gneiss.material","version":2,"color":[1,0,0,1],"base_color_texture":"asset://missing"})"
            : R"({"format":"gneiss.material","version":1,"color":[1,0,0,1]})");
    caors::resources::handle<material_document> candidate;
    unsigned decodes = 0;
    const std::array requested{asset_request{"asset://material.json", asset_type::material}};
    asset_preparation preparation(
        files, requested,
        {.material_bytes = 256, .maximum_assets = 8, .maximum_bytes = document_limit}, {},
        [&](const auto& bytes, auto& parameters, auto& uris, auto& diagnostic) {
          check(std::this_thread::get_id() != host_thread, "batch decode runs off host");
          ++decodes;
          candidate = caors::resources::make_handle<material_document>(nullptr, id);
          if (!candidate)
            return GNEISS_ERROR_OUT_OF_MEMORY;
          const auto decoded = documents->decode(bytes, parameters, uris, diagnostic);
          check(decode_material_document(bytes, *candidate, diagnostic) == decoded, "same decoder");
          if (decoded == GNEISS_SUCCESS) {
            parameters = candidate->source.parameters;
            uris = candidate->source.texture_uris;
            if (change_source)
              write("{}");
          }
          return decoded;
        });
    prepared_batch batch;
    asset_diagnostic diagnostic;
    bool complete = false;
    gneiss_result status = GNEISS_SUCCESS;
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (!complete) {
      std::thread background([&] {
        status =
            preparation.advance(document_limit, [] { return false; }, batch, diagnostic, complete);
      });
      background.join();
      check(std::chrono::steady_clock::now() < deadline, "batch timeout");
      if (!complete)
        std::this_thread::sleep_for(1ms);
    }
    check(decodes == 1, "batch decodes once");
    if (change_source || dependency_missing) {
      check(status != GNEISS_SUCCESS && batch.assets.empty(), "unverified batch not published");
      check(cache.find_handle(id).get() == current.get(), "failed batch keeps old document");
    } else {
      check(status == GNEISS_SUCCESS && batch.assets.size() == 1, "verified material batch");
      check(documents->retain_verified(batch) == GNEISS_SUCCESS, "retain verified documents");
      const auto* retained = dynamic_cast<const material_document_lease*>(
          batch.assets.front().material_document.get());
      check(retained && *retained, "typed retained document");
      auto second = shared_documents->begin();
      std::vector<std::byte> bytes;
      check(files.read("asset://material.json", bytes) == GNEISS_SUCCESS, "read second document");
      material_parameters parameters;
      std::array<std::string, 5> uris;
      check(second->decode(bytes, parameters, uris, diagnostic) == GNEISS_SUCCESS, "decode second");
      auto second_batch = batch;
      second_batch.assets.front().material_document.reset();
      check(second->retain_verified(second_batch) == GNEISS_SUCCESS, "retain second");
      const auto* reused = dynamic_cast<const material_document_lease*>(
          second_batch.assets.front().material_document.get());
      check(reused && reused->get() == retained->get(), "cross batch document shared");
      second_batch = {};
      shared_documents->collect();
      check(retained->get()->source.parameters.color[0] == 1,
            "first lease survives second release");
      const auto& prepared = batch.assets.front();
      check(!prepared.version.version.empty() && prepared.material.color[0] == 1,
            "batch version and data");
      material_document_lease lease;
      check(versions.remember(prepared.sources.front(), candidate, lease) == result::success,
            "register only verified batch");
      check(lease.get()->source.parameters.color[0] == prepared.material.color[0],
            "lease matches batch");
    }
  };
  prepare(false, false);
  prepare(true, false);
  prepare(false, true);
  write(std::string(document_limit + 1U, ' '));
  auto oversized = manager.request_reload<material_document>(id);
  wait([&] { return oversized.done(); });
  check(oversized.failed() && cache.find_handle(id).get() == current.get(),
        "limit preserves current");
  check(cache.erase(id) && old->source.parameters.color[0] == 1 &&
            current->source.parameters.color[1] == 1,
        "handles survive eviction");
}
}
int main() {
  try {
    run(1024U * 1024U);
    run(2U * 1024U * 1024U);
    return 0;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
  }
}
