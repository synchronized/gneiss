// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "engine/core/tasks/task_scheduler.hpp"
#include "engine/resource/loaders/material_description_decoder.hpp"
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

static_assert(!std::is_polymorphic_v<gneiss::engine::material_description>);

namespace {
void check(bool ok, const char* message) {
  if (!ok)
    throw std::runtime_error(message);
}
void run(std::size_t document_limit) {
  using namespace gneiss::engine;
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
  gneiss::engine::uv_loop_executor loop;
  check(loop.start() == gneiss::engine::result::success, "loop");
  auto native = std::make_shared<native_file_system>();
  check(native->initialize(root.generic_string()) == gneiss::engine::result::success,
        "native root");
  native->set_async_io(std::make_shared<gneiss::engine::uv_file_io>(loop),
                       std::make_shared<gneiss::engine::uv_path_resolver>(loop));
  virtual_file_system files;
  check(files.mount("asset://", native) == gneiss::engine::result::success, "mount");
  auto old = std::make_shared<material_description>();
  auto current = std::make_shared<material_description>();
  std::vector<std::byte> document_bytes;
  asset_diagnostic decode_diagnostic;
  check(files.read("asset://material.json", document_bytes) == gneiss::engine::result::success,
        "read old");
  check(decode_material_description(document_bytes, *old, decode_diagnostic) ==
            gneiss::engine::result::success,
        "decode old");
  write(R"({"format":"gneiss.material","version":1,"color":[0,1,0,1]})");
  check(files.read("asset://material.json", document_bytes) == gneiss::engine::result::success,
        "read new");
  check(decode_material_description(document_bytes, *current, decode_diagnostic) ==
            gneiss::engine::result::success,
        "decode new");
  // 直接走生产使用的分步准备：复用已读取字节，不再从 Loader 启动第二次读取。
  auto prepare = [&](bool change_source, bool dependency_missing) {
    write(
        dependency_missing
            ? R"({"format":"gneiss.material","version":2,"color":[1,0,0,1],"base_color_texture":"asset://missing"})"
            : R"({"format":"gneiss.material","version":1,"color":[1,0,0,1]})");
    const std::array requested{
        asset_preparation_source{"asset://material.json", asset_type::material}};
    asset_preparation preparation(
        files, requested,
        {.material_bytes = 256, .maximum_assets = 8, .maximum_bytes = document_limit}, {});
    prepared_batch batch;
    asset_diagnostic diagnostic;
    bool complete = false;
    gneiss::engine::result status = gneiss::engine::result::success;
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
    if (dependency_missing) {
      check(status != gneiss::engine::result::success && batch.assets.empty(),
            "failed batch not returned");
      check(current->parameters.color[1] == 1, "failed batch keeps old document");
    } else {
      check(status == gneiss::engine::result::success && batch.assets.size() == 1,
            "prepared material batch");
      if (change_source)
        write("{}");
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
