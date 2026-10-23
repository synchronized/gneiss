// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/tasks/task_scheduler.hpp"
#include "engine/resource/cache/cpu_document_cache.hpp"
#include "engine/resource/cache/material_document_versions.hpp"
#include "engine/resource/loaders/material_document_decoder.hpp"
#include "engine/resource/vfs/native_file_system.hpp"
#include "engine/resource/vfs/virtual_file_system.hpp"
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

static_assert(!std::is_polymorphic_v<gneiss::engine::resource::material_document_lease>);

namespace {
void check(bool ok, const char* message) {
  if (!ok)
    throw std::runtime_error(message);
}
void run(std::size_t document_limit) {
  using namespace gneiss::engine::resource;
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
  gneiss::engine::platform::io::uv_loop_executor loop;
  check(loop.start() == result::success, "loop");
  auto native = std::make_shared<native_file_system>();
  check(native->initialize(root.generic_string()) == GNEISS_SUCCESS, "native root");
  native->set_async_io(std::make_shared<gneiss::engine::platform::io::uv_file_io>(loop),
                       std::make_shared<gneiss::engine::platform::io::uv_path_resolver>(loop));
  virtual_file_system files;
  check(files.mount("asset://", native) == GNEISS_SUCCESS, "mount");
  const auto host_thread = std::this_thread::get_id();
  auto old = std::make_shared<material_document>();
  auto current = std::make_shared<material_document>();
  std::vector<std::byte> document_bytes;
  asset_diagnostic decode_diagnostic;
  check(files.read("asset://material.json", document_bytes) == GNEISS_SUCCESS, "read old");
  check(decode_material_document(document_bytes, *old, decode_diagnostic) == GNEISS_SUCCESS,
        "decode old");
  write(R"({"format":"gneiss.material","version":1,"color":[0,1,0,1]})");
  check(files.read("asset://material.json", document_bytes) == GNEISS_SUCCESS, "read new");
  check(decode_material_document(document_bytes, *current, decode_diagnostic) == GNEISS_SUCCESS,
        "decode new");
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
  auto duplicate = std::make_shared<material_document>();
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
  // 直接走生产使用的分步准备：复用已读取字节，不再从 Loader 启动第二次读取。
  auto shared_documents = std::make_shared<cpu_document_cache>();
  check(shared_documents != nullptr, "material cache enabled");
  auto prepare = [&](bool change_source, bool dependency_missing) {
    auto documents = shared_documents->begin();
    write(
        dependency_missing
            ? R"({"format":"gneiss.material","version":2,"color":[1,0,0,1],"base_color_texture":"asset://missing"})"
            : R"({"format":"gneiss.material","version":1,"color":[1,0,0,1]})");
    std::shared_ptr<material_document> candidate;
    unsigned decodes = 0;
    const std::array requested{
        asset_preparation_source{"asset://material.json", asset_type::material}};
    asset_preparation preparation(
        files, requested,
        {.material_bytes = 256, .maximum_assets = 8, .maximum_bytes = document_limit}, {},
        [&](const auto& bytes, auto& parameters, auto& uris, auto& diagnostic) {
          check(std::this_thread::get_id() != host_thread, "batch decode runs off host");
          ++decodes;
          candidate = std::make_shared<material_document>();
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
      check(current->source.parameters.color[1] == 1, "failed batch keeps old document");
    } else {
      check(status == GNEISS_SUCCESS && batch.assets.size() == 1, "verified material batch");
      check(documents->retain_verified(batch) == GNEISS_SUCCESS, "retain verified documents");
      const auto* retained = batch.assets.front().material_document.get();
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
      const auto* reused = second_batch.assets.front().material_document.get();
      check(reused && reused->get() == retained->get(), "cross batch document shared");
      second_batch = {};
      shared_documents->collect();
      check(retained->get()->source.parameters.color[0] == 1,
            "first lease survives second release");
      // 外壳先释放，两个在途批次仍共享版本索引；租约可再晚于批次释放。
      auto temporary_cache = std::make_shared<cpu_document_cache>();
      auto detached_first = temporary_cache->begin();
      auto detached_second = temporary_cache->begin();
      temporary_cache.reset();
      auto detached_batch = batch;
      detached_batch.assets.front().material_document.reset();
      check(detached_first->decode(bytes, parameters, uris, diagnostic) == GNEISS_SUCCESS,
            "decode after cache shell destruction");
      check(detached_first->retain_verified(detached_batch) == GNEISS_SUCCESS,
            "retain after cache shell destruction");
      auto kept = detached_batch.assets.front().material_document;
      detached_batch.assets.front().material_document.reset();
      detached_first.reset();
      check(detached_second->decode(bytes, parameters, uris, diagnostic) == GNEISS_SUCCESS,
            "second detached batch decode");
      check(detached_second->retain_verified(detached_batch) == GNEISS_SUCCESS &&
                detached_batch.assets.front().material_document->get() == kept->get(),
            "detached batches share version index");
      detached_second.reset();
      detached_batch = {};
      check(kept->get()->source.parameters.color[0] == 1, "lease survives cache and all batches");
      const auto& prepared = batch.assets.front();
      check(!prepared.sources.empty() && prepared.material.color[0] == 1, "batch version and data");
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
