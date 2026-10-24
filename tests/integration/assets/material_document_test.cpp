// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "engine/core/tasks/task_scheduler.hpp"
#include "engine/resource/cache/cpu_document_cache.hpp"
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

static_assert(!std::is_polymorphic_v<gneiss::engine::resource::material_document>);

namespace {
void check(bool ok, const char* message) {
  if (!ok)
    throw std::runtime_error(message);
}
void run(std::size_t document_limit) {
  using namespace gneiss::engine::resource;
  using namespace std::chrono_literals;

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
  check(loop.start() == gneiss::engine::core::result::success, "loop");
  auto native = std::make_shared<native_file_system>();
  check(native->initialize(root.generic_string()) == gneiss::engine::core::result::success,
        "native root");
  native->set_async_io(std::make_shared<gneiss::engine::platform::io::uv_file_io>(loop),
                       std::make_shared<gneiss::engine::platform::io::uv_path_resolver>(loop));
  virtual_file_system files;
  check(files.mount("asset://", native) == gneiss::engine::core::result::success, "mount");
  const auto host_thread = std::this_thread::get_id();
  auto old = std::make_shared<material_document>();
  auto current = std::make_shared<material_document>();
  std::vector<std::byte> document_bytes;
  asset_diagnostic decode_diagnostic;
  check(files.read("asset://material.json", document_bytes) ==
            gneiss::engine::core::result::success,
        "read old");
  check(decode_material_document(document_bytes, *old, decode_diagnostic) ==
            gneiss::engine::core::result::success,
        "decode old");
  write(R"({"format":"gneiss.material","version":1,"color":[0,1,0,1]})");
  check(files.read("asset://material.json", document_bytes) ==
            gneiss::engine::core::result::success,
        "read new");
  check(decode_material_document(document_bytes, *current, decode_diagnostic) ==
            gneiss::engine::core::result::success,
        "decode new");
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
            return gneiss::engine::core::result::out_of_memory;
          const auto decoded = documents->decode(bytes, parameters, uris, diagnostic);
          check(decode_material_document(bytes, *candidate, diagnostic) == decoded, "same decoder");
          if (decoded == gneiss::engine::core::result::success) {
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
    gneiss::engine::core::result status = gneiss::engine::core::result::success;
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
    if (dependency_missing) {
      check(status != gneiss::engine::core::result::success && batch.assets.empty(),
            "failed batch not returned");
      check(current->source.parameters.color[1] == 1, "failed batch keeps old document");
    } else {
      check(status == gneiss::engine::core::result::success && batch.assets.size() == 1,
            "prepared material batch");
      check(documents->retain_prepared(batch) == gneiss::engine::core::result::success,
            "retain prepared documents");
      const auto* retained = batch.assets.front().material_document.get();
      check(retained != nullptr, "typed retained document");
      if (change_source) {
        check(retained->source.parameters.color[0] == 1, "retain bytes read before file change");
        return;
      }
      auto second = shared_documents->begin();
      std::vector<std::byte> bytes;
      check(files.read("asset://material.json", bytes) == gneiss::engine::core::result::success,
            "read second document");
      material_parameters parameters;
      std::array<std::string, 5> uris;
      check(second->decode(bytes, parameters, uris, diagnostic) ==
                gneiss::engine::core::result::success,
            "decode second");
      auto second_batch = batch;
      second_batch.assets.front().material_document.reset();
      check(second->retain_prepared(second_batch) == gneiss::engine::core::result::success,
            "retain second");
      const auto* reused = second_batch.assets.front().material_document.get();
      check(reused && reused == retained, "cross batch document shared");
      second_batch = {};
      shared_documents->collect();
      check(retained->source.parameters.color[0] == 1, "first lease survives second release");
      // 外壳先释放，两个在途批次仍共享内容索引；租约可再晚于批次释放。
      auto temporary_cache = std::make_shared<cpu_document_cache>();
      auto detached_first = temporary_cache->begin();
      auto detached_second = temporary_cache->begin();
      temporary_cache.reset();
      auto detached_batch = batch;
      detached_batch.assets.front().material_document.reset();
      check(detached_first->decode(bytes, parameters, uris, diagnostic) ==
                gneiss::engine::core::result::success,
            "decode after cache shell destruction");
      check(detached_first->retain_prepared(detached_batch) ==
                gneiss::engine::core::result::success,
            "retain after cache shell destruction");
      auto kept = detached_batch.assets.front().material_document;
      detached_batch.assets.front().material_document.reset();
      detached_first.reset();
      check(detached_second->decode(bytes, parameters, uris, diagnostic) ==
                gneiss::engine::core::result::success,
            "second detached batch decode");
      check(detached_second->retain_prepared(detached_batch) ==
                    gneiss::engine::core::result::success &&
                detached_batch.assets.front().material_document.get() == kept.get(),
            "detached batches share content index");
      detached_second.reset();
      detached_batch = {};
      check(kept->source.parameters.color[0] == 1, "lease survives cache and all batches");
      const auto& prepared = batch.assets.front();
      check(!prepared.sources.empty() && prepared.material.color[0] == 1, "batch version and data");
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
