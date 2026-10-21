// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "uv_loop_executor.hpp"
#include "uv_path_resolver.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <semaphore>
#include <source_location>
#include <stdexcept>

namespace {
using gneiss::result;
using gneiss::engine::platform::io::uv_loop_executor;
using gneiss::engine::platform::io::uv_path_operation;
using gneiss::engine::platform::io::uv_path_resolver;

void check(bool value, std::source_location where = std::source_location::current()) {
  if (!value) {
    throw std::runtime_error("异步路径测试失败，行=" + std::to_string(where.line()));
  }
}
std::string utf8(const std::filesystem::path& path) {
  const auto text = path.u8string();
  return {text.begin(), text.end()};
}
struct fixture {
  std::filesystem::path root =
      std::filesystem::temp_directory_path() /
      ("gneiss-uv-path-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fixture() { check(std::filesystem::create_directory(root)); }
  ~fixture() {
    std::error_code error;
    try {
      std::filesystem::remove_all(root, error);
    } catch (...) {
      std::fprintf(stderr, "异步路径测试目录清理异常\n");
    }
  }
};
struct gate {
  std::shared_ptr<std::binary_semaphore> release = std::make_shared<std::binary_semaphore>(0);
  ~gate() { release->release(); }
};
void run() {
  fixture files;
  const auto path = files.root / std::filesystem::path(u8"路径.bin");
  {
    std::ofstream stream(path);
    stream << "test";
  }
  std::unique_ptr<uv_path_operation> successful;
  std::unique_ptr<uv_path_operation> missing;
  {
    uv_loop_executor executor;
    uv_path_resolver resolver(executor, 1U);
    check(resolver.begin(utf8(path), successful) == result::not_ready && !successful);
    check(executor.start() == result::success);
    check(resolver.begin("", successful) == result::invalid_argument);
    check(resolver.begin(std::string("bad\0path", 8U), successful) == result::invalid_argument);
    {
      gate blocked;
      auto ready = blocked.release;
      check(executor.post([ready] { ready->acquire(); }) == result::success);
      check(resolver.begin(utf8(path), successful) == result::success);
      std::string output = "old";
      check(successful->poll(output) == result::not_ready && output.empty());
      check(resolver.begin(utf8(path), missing) == result::not_ready && !missing);
      successful.reset();
      // 放弃不会等待 loop，也不会提前归还仍在排队的容量。
      check(resolver.begin(utf8(path), missing) == result::not_ready);
    }
    check(executor.stop() == result::success);
    check(executor.start() == result::success);
    check(resolver.begin(utf8(path), successful) == result::success);
    check(executor.stop() == result::success);
    check(executor.start() == result::success);
    {
      uv_path_resolver temporary(executor);
      check(temporary.begin(utf8(files.root / "missing"), missing) == result::success);
    }
    check(executor.stop() == result::success);
    check(executor.failed_task_count() == 0U);
  }
  // 回执不借用解析器或执行器；停机排空后仍可接收，成功只交付一次。
  std::string output;
  check(successful->poll(output) == result::success);
  check(std::filesystem::equivalent(
      std::filesystem::path(std::u8string(output.begin(), output.end())), path));
  check(successful->poll(output) == result::invalid_state && output.empty());
  check(missing->poll(output) == result::not_found && output.empty());
  check(missing->poll(output) == result::not_found);
}
} // namespace

int main() try {
  run();
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
