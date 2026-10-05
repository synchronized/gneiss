// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "application/application_asset_reload_internal.hpp"
#include "asset/texture_ktx2.h"

#include <gneiss/application.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace {
void check(bool value) {
  if (!value) {
    throw std::runtime_error("Application 纹理注入失败");
  }
}
struct fixture {
  std::filesystem::path root =
      std::filesystem::temp_directory_path() /
      ("gneiss-texture-app-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fixture() { std::filesystem::create_directories(root); }
  ~fixture() {
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
  }
};
void run(gneiss::tasks::execution_mode mode) {
  using namespace gneiss;
  fixture files;
  std::ofstream(files.root / "a.texture.json")
      << R"({"format":"gneiss.texture","version":1,"source":"asset://image.ktx2","color_space":"srgb"})";
  std::vector<std::byte> bytes;
  std::string diagnostic;
  check(asset_internal::encode_texture_ktx2(
            {.transfer = asset_internal::texture_transfer::srgb,
             .levels = {{.width = 1U,
                         .height = 1U,
                         .pixels = std::vector<std::byte>(4U, std::byte{255})}}},
            bytes, diagnostic) == asset_internal::texture_ktx2_result::success);
  {
    std::ofstream stream(files.root / "image.ktx2", std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
  }
  tasks::task_scheduler scheduler({.workers = 1U, .mode = mode});
  application app;
  const auto root = files.root.string();
  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  desc.asset_root = root.data();
  desc.asset_root_length = static_cast<std::uint32_t>(root.size());
  check(application::create(desc, app) == result::success);
  check(application_internal::attach_task_executor(app.get(), scheduler) == GNEISS_SUCCESS);
  check(application_internal::attach_task_executor(app.get(), scheduler) ==
        GNEISS_ERROR_INVALID_STATE);
  const std::vector<std::string> uris{"asset://a.texture.json"};
  std::uint64_t request{};
  check(application_internal::request_textures(app.get(), uris, 2U, 1U, request, false) ==
        GNEISS_SUCCESS);
  asset_internal::texture_load_completion completion;
  bool ready{};
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!ready) {
    if (mode == tasks::execution_mode::cooperative) {
      (void)scheduler.run_ready();
    }
    check(application_internal::poll_textures(app.get(), completion, ready) == GNEISS_SUCCESS);
    check(std::chrono::steady_clock::now() < deadline);
    std::this_thread::yield();
  }
  check(completion.result == GNEISS_SUCCESS && completion.request == request &&
        completion.textures.size() == 1U);
  completion = {}; // 所有租约必须先于 Application 销毁。
  check(application_internal::request_textures(app.get(), uris, 2U, 2U, request) == GNEISS_SUCCESS);
  check(application_internal::cancel_textures(app.get()) == GNEISS_SUCCESS);
  // app 析构完成取消与作用域回收；注入的宿主池仍然有效。
}
}
int main() try {
  run(gneiss::tasks::execution_mode::cooperative);
  run(gneiss::tasks::execution_mode::thread_pool);
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
