// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "uv_async_file.hpp"
#include "uv_loop_executor.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <semaphore>
#include <source_location>
#include <stdexcept>
#include <thread>

namespace {
using gneiss::engine::result;
using namespace gneiss::engine;
void check(bool condition, std::source_location location = std::source_location::current()) {
  if (!condition) {
    throw std::runtime_error("异步文件测试失败，行=" + std::to_string(location.line()));
  }
}
std::string utf8(const std::filesystem::path& path) {
  const auto text = path.u8string();
  return {text.begin(), text.end()};
}
struct fixture {
  std::filesystem::path root =
      std::filesystem::temp_directory_path() /
      ("gneiss-uv-async-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fixture() { check(std::filesystem::create_directory(root)); }
  ~fixture() {
    try {
      std::error_code error;
      std::filesystem::remove_all(root, error);
    } catch (...) {
      std::fputs("异步文件测试清理失败\n", stderr);
    }
  }
};
gneiss::engine::result open(uv_file_io& io, const std::filesystem::path& path,
                            std::shared_ptr<uv_async_file>& output) {
  std::unique_ptr<uv_file_open_operation> pending;
  auto value = io.begin_open(utf8(path), pending);
  if (value != gneiss::engine::result::success) {
    return value;
  }
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  do {
    value = pending->poll(output);
    if (value != gneiss::engine::result::not_ready) {
      return value;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  } while (std::chrono::steady_clock::now() < deadline);
  return gneiss::engine::result::not_ready;
}
struct gate {
  std::shared_ptr<std::binary_semaphore> signal = std::make_shared<std::binary_semaphore>(0);
  ~gate() { signal->release(); }
};
void run() {
  fixture files;
  const auto path = files.root / std::filesystem::path(u8"区间.bin");
  {
    std::ofstream stream(path, std::ios::binary);
    stream << "abcdefgh";
  }
  uv_loop_executor executor;
  check(executor.start() == gneiss::engine::result::success);
  uv_file_io io(executor, {.requests = 4U, .bytes = 8U});
  std::shared_ptr<uv_async_file> source;
  check(open(io, files.root / "missing", source) == gneiss::engine::result::not_found && !source);
  check(open(io, files.root, source) != gneiss::engine::result::success && !source);
  check(open(io, path, source) == gneiss::engine::result::success && source->size() == 8U);
  std::unique_ptr<uv_file_read_operation> first;
  std::unique_ptr<uv_file_read_operation> second;
  std::span<const std::byte> bytes;
  check(source->begin_read(9U, 0U, first) == gneiss::engine::result::invalid_argument);
  check(source->begin_read(7U, 2U, first) == gneiss::engine::result::invalid_argument);
  check(source->begin_read(1U, 5U, first) == gneiss::engine::result::success);
  check(first->wait_for(std::chrono::seconds(5), bytes) == gneiss::engine::result::success &&
        bytes.size() == 5U && bytes.front() == std::byte{'b'} && bytes.back() == std::byte{'f'});
  check(source->begin_read(0U, 4U, second) == gneiss::engine::result::not_ready && !second);
  first.reset();
  check(source->begin_read(8U, 0U, first) == gneiss::engine::result::success);
  check(first->wait_for(std::chrono::seconds(5), bytes) == gneiss::engine::result::success &&
        bytes.empty());
  first.reset();
  const auto moved = files.root / "moved.bin";
  std::filesystem::rename(path, moved);
  {
    std::ofstream stream(path);
    stream << "xxxxxxxx";
  }
  check(source->begin_read(0U, 4U, first) == gneiss::engine::result::success);
  check(source->begin_read(4U, 4U, second) == gneiss::engine::result::success);
  source.reset();
  check(first->wait_for(std::chrono::seconds(5), bytes) == gneiss::engine::result::success &&
        bytes.front() == std::byte{'a'});
  check(second->wait_for(std::chrono::seconds(5), bytes) == gneiss::engine::result::success &&
        bytes.front() == std::byte{'e'});
  first.reset();
  second.reset();
  check(open(io, moved, source) == gneiss::engine::result::success);
  {
    gate blocked;
    std::binary_semaphore entered(0);
    check(executor.post([signal = blocked.signal, &entered] {
      entered.release();
      signal->acquire();
    }) == gneiss::engine::result::success);
    entered.acquire();
    check(source->begin_read(0U, 4U, first) == gneiss::engine::result::success);
    source->begin_close();
    source->begin_close();
    check(source->poll_close() == gneiss::engine::result::not_ready);
    check(source->begin_read(0U, 1U, second) == gneiss::engine::result::invalid_state);
    std::byte buffer{};
    check(source->read_at(0U, {&buffer, 1U}) == gneiss::engine::result::invalid_state);
  }
  check(first->wait_for(std::chrono::seconds(5), bytes) == gneiss::engine::result::success &&
        bytes.front() == std::byte{'a'});
  const auto close_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (source->poll_close() == gneiss::engine::result::not_ready &&
         std::chrono::steady_clock::now() < close_deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  check(source->poll_close() == gneiss::engine::result::success);
  source->begin_close();
  check(source->poll_close() == gneiss::engine::result::success);
  first.reset();
  source.reset();
  check(open(io, moved, source) == gneiss::engine::result::success);
  std::filesystem::resize_file(moved, 2U);
  check(source->begin_read(0U, 8U, first) == gneiss::engine::result::success);
  check(first->wait_for(std::chrono::seconds(5), bytes) == gneiss::engine::result::io &&
        bytes.empty());
  first.reset();
  {
    gate blocked;
    std::binary_semaphore entered(0);
    check(executor.post([signal = blocked.signal, &entered] {
      entered.release();
      signal->acquire();
    }) == gneiss::engine::result::success);
    entered.acquire();
    check(source->begin_read(0U, 2U, first) == gneiss::engine::result::success);
    check(first->poll(bytes) == gneiss::engine::result::not_ready);
    first.reset();
    source.reset();
    std::unique_ptr<uv_file_open_operation> abandoned;
    check(io.begin_open(utf8(path), abandoned) == gneiss::engine::result::success);
    abandoned.reset();
  }
  check(executor.stop() == gneiss::engine::result::success);
  check(executor.failed_task_count() == 0U);
}
} // namespace
int main() try {
  run();
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
