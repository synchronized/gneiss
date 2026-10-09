// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/asset/native_file_system.hpp"
#include "engine/asset/read_slice.hpp"
#include "engine/asset/virtual_file_system.hpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <future>
#include <limits>
#include <source_location>
#include <stdexcept>
#include <thread>

namespace {
using namespace gneiss::asset_internal;

void check(bool value, std::source_location where = std::source_location::current()) {
  if (!value) {
    throw std::runtime_error("区间读取契约失败，行=" + std::to_string(where.line()));
  }
}

struct temporary_directory {
  std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      ("gneiss-read-source-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  temporary_directory() { check(std::filesystem::create_directory(path)); }
  ~temporary_directory() try {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  } catch (...) {
    // 测试退出清理不得以异常覆盖原始失败。
    std::fprintf(stderr, "读取来源测试临时目录清理失败\n");
  }
};

struct legacy_files final : file_system {
  gneiss_result read(std::string_view /*path*/,
                     std::vector<std::byte>& /*out_bytes*/) const noexcept override {
    return GNEISS_ERROR_IO;
  }
};

struct deferred_state {
  bool ready{};
  bool fail{};
  bool short_read{};
  unsigned live{};
  unsigned waits{};
  bool wake_on_wait{};
};
class deferred_source final : public read_source {
public:
  std::shared_ptr<deferred_state> state = std::make_shared<deferred_state>();
  [[nodiscard]] std::uint64_t size() const noexcept override { return 8U; }
  [[nodiscard]] gneiss_result read_at(std::uint64_t /*offset*/,
                                      std::span<std::byte> /*output*/) const noexcept override {
    return GNEISS_ERROR_UNSUPPORTED;
  }
  [[nodiscard]] gneiss_result
  begin_read(std::uint64_t offset, std::size_t count,
             std::unique_ptr<read_operation>& output) const noexcept override {
    class operation final : public read_operation {
    public:
      operation(std::shared_ptr<deferred_state> state, std::size_t count)
          : state_(std::move(state)), bytes_(count, std::byte{42}) {
        ++state_->live;
      }
      ~operation() override { --state_->live; }
      [[nodiscard]] gneiss_result poll(std::span<const std::byte>& output) noexcept override {
        output = {};
        if (!state_->ready) {
          return GNEISS_ERROR_NOT_READY;
        }
        if (state_->fail) {
          return GNEISS_ERROR_IO;
        }
        output = std::span{bytes_}.first(bytes_.size() - (state_->short_read ? 1U : 0U));
        return GNEISS_SUCCESS;
      }

      gneiss_result wait_for(std::chrono::milliseconds timeout,
                             std::span<const std::byte>& output) noexcept override {
        ++state_->waits;
        if (timeout != std::chrono::milliseconds(4)) {
          return GNEISS_ERROR_INVALID_ARGUMENT;
        }
        state_->ready = state_->wake_on_wait;
        return poll(output);
      }

    private:
      std::shared_ptr<deferred_state> state_;
      std::vector<std::byte> bytes_;
    };
    output.reset();
    if (offset > size() || count > size() - offset) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    try {
      output = std::make_unique<operation>(state, count);
      return GNEISS_SUCCESS;
    } catch (...) {
      return GNEISS_ERROR_OUT_OF_MEMORY;
    }
  }
};

void deferred_reads() {
  deferred_source source;
  read_slice slice;
  std::span<const std::byte> bytes;
  std::unique_ptr<read_operation> primed;
  check(source.begin_read(0U, 8U, primed) == GNEISS_SUCCESS);
  slice.prime(std::move(primed), {.offset = 0U, .size = 8U});
  check(slice.take(source, {.offset = 0U, .size = 8U}, bytes) == GNEISS_ERROR_NOT_READY &&
        bytes.empty());
  check(source.state->live == 1U);
  check(slice.take(source, {.offset = 0U, .size = 2U}, bytes) == GNEISS_ERROR_NOT_READY);
  check(source.state->waits == 0U);
  check(slice.take(source, {.offset = 0U, .size = 2U}, bytes, true, std::chrono::milliseconds(4)) ==
        GNEISS_ERROR_NOT_READY);
  check(source.state->waits == 1U);
  source.state->wake_on_wait = true;
  check(slice.take(source, {.offset = 0U, .size = 2U}, bytes, true, std::chrono::milliseconds(4)) ==
            GNEISS_SUCCESS &&
        bytes.size() == 2U);
  check(bytes.front() == std::byte{42});
  check(slice.take(source, {.offset = 2U, .size = 6U}, bytes) == GNEISS_SUCCESS &&
        bytes.size() == 6U);
  slice = {};
  check(source.state->live == 0U);
  source.state->ready = false;
  check(slice.take(source, {.offset = 0U, .size = 8U}, bytes) == GNEISS_ERROR_NOT_READY);
  slice = {}; // 未完成请求的取消回收不能依赖来源或栈输出缓冲。
  check(source.state->live == 0U);
  source.state->ready = true;
  source.state->short_read = true;
  check(slice.take(source, {.offset = 0U, .size = 8U}, bytes) == GNEISS_ERROR_IO);
  slice = {};
  source.state->short_read = false;
  source.state->fail = true;
  check(slice.take(source, {.offset = 0U, .size = 8U}, bytes) == GNEISS_ERROR_IO);
}

#ifdef _WIN32
void native_deferred_reads() {
  temporary_directory directory;
  {
    std::ofstream file(directory.path / "data", std::ios::binary);
    file << "abcdefgh";
  }
  std::unique_ptr<read_operation> first;
  std::unique_ptr<read_operation> second;
  {
    native_file_system files;
    check(files.initialize(directory.path.string()) == GNEISS_SUCCESS);
    std::unique_ptr<read_source> source;
    check(files.open_read("data", source) == GNEISS_SUCCESS);
    check(source->begin_read(9U, 1U, first) == GNEISS_ERROR_INVALID_ARGUMENT && !first);
    check(source->begin_read(2U, 3U, first) == GNEISS_SUCCESS);
    check(source->begin_read(0U, 8U, second) == GNEISS_SUCCESS);
    std::unique_ptr<read_operation> empty;
    std::span<const std::byte> bytes;
    check(source->begin_read(8U, 0U, empty) == GNEISS_SUCCESS);
    check(empty->poll(bytes) == GNEISS_SUCCESS && bytes.empty());
    check(empty->wait_for(std::chrono::milliseconds(0), bytes) == GNEISS_SUCCESS && bytes.empty());
    check(empty->wait_for(std::chrono::milliseconds(-1), bytes) == GNEISS_ERROR_INVALID_ARGUMENT);
    check(empty->wait_for(std::chrono::milliseconds(UINT32_MAX), bytes) ==
          GNEISS_ERROR_INVALID_ARGUMENT);
    std::unique_ptr<read_operation> discarded;
    check(source->begin_read(0U, 8U, discarded) == GNEISS_SUCCESS);
    discarded.reset(); // 完成或仍在途均须安全取消/回收，不要求内核制造特定时序。
  }
  // 来源、VFS 销毁和路径替换不改变在途请求指向的打开对象。
  std::filesystem::rename(directory.path / "data", directory.path / "previous");
  {
    std::ofstream file(directory.path / "data", std::ios::binary);
    file << "new-data";
  }
  const auto wait = [](read_operation& operation, std::size_t size, std::byte expected) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    std::span<const std::byte> bytes;
    auto result = operation.wait_for(std::chrono::milliseconds(4), bytes);
    while (result == GNEISS_ERROR_NOT_READY && std::chrono::steady_clock::now() < deadline) {
      result = operation.wait_for(std::chrono::milliseconds(4), bytes);
    }
    check(result == GNEISS_SUCCESS && bytes.size() == size && bytes.front() == expected);
    check(operation.poll(bytes) == GNEISS_SUCCESS && bytes.front() == expected);
  };
  wait(*first, 3U, std::byte{'c'});
  wait(*second, 8U, std::byte{'a'});
  native_file_system files;
  check(files.initialize(directory.path.string()) == GNEISS_SUCCESS);
  std::unique_ptr<read_source> source;
  check(files.open_read("data", source) == GNEISS_SUCCESS);
  std::filesystem::remove(directory.path / "data");
  std::unique_ptr<read_operation> after_remove;
  check(source->begin_read(0U, 8U, after_remove) == GNEISS_SUCCESS);
  wait(*after_remove, 8U, std::byte{'n'});
  check(files.open_read("previous", source) == GNEISS_SUCCESS);
  std::filesystem::resize_file(directory.path / "previous", 2U);
  std::unique_ptr<read_operation> truncated;
  auto result = source->begin_read(4U, 3U, truncated);
  if (result == GNEISS_SUCCESS) {
    std::span<const std::byte> bytes;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    do {
      result = truncated->poll(bytes);
      std::this_thread::yield();
    } while (result == GNEISS_ERROR_NOT_READY && std::chrono::steady_clock::now() < deadline);
  }
  check(result == GNEISS_ERROR_IO);
}
#endif

void run() {
  temporary_directory directory;
  {
    std::ofstream file(directory.path / "data", std::ios::binary);
    file << "abcdefgh";
  }
  {
    std::ofstream empty(directory.path / "empty", std::ios::binary);
  }
  std::unique_ptr<read_source> source;
  {
    auto backend = std::make_shared<native_file_system>();
    check(backend->open_read("data", source) == GNEISS_ERROR_INVALID_STATE && !source);
    check(backend->initialize(directory.path.string()) == GNEISS_SUCCESS);
    virtual_file_system vfs;
    check(vfs.open_read("asset://data", source) == GNEISS_ERROR_NOT_FOUND);
    check(vfs.mount("asset://", backend) == GNEISS_SUCCESS);
    check(vfs.mount("asset://legacy/", std::make_shared<legacy_files>()) == GNEISS_SUCCESS);
    check(vfs.open_read("asset://data", source) == GNEISS_SUCCESS && source->size() == 8U);
    // 最长挂载优先，且失败不得保留上一次输出。
    check(vfs.open_read("asset://legacy/data", source) == GNEISS_ERROR_UNSUPPORTED && !source);
    check(vfs.open_read("asset://../data", source) == GNEISS_ERROR_INVALID_ARGUMENT && !source);
    check(vfs.open_read("asset://missing", source) == GNEISS_ERROR_NOT_FOUND && !source);
    check(vfs.open_read("asset://empty", source) == GNEISS_SUCCESS && source->size() == 0U);
    check(source->read_at(0U, {}) == GNEISS_SUCCESS);
    check(source->read_at(1U, {}) == GNEISS_ERROR_INVALID_ARGUMENT);
    std::vector<std::byte> whole;
    check(vfs.read_bounded("asset://data", 8U, whole) == GNEISS_SUCCESS && whole.size() == 8U);
    check(vfs.open_read("asset://data", source) == GNEISS_SUCCESS);
  }
  // VFS 与后端析构后来源仍拥有打开对象。
  std::array<std::byte, 3> bytes{};
  check(source->read_at(2U, bytes) == GNEISS_SUCCESS);
  check(bytes[0] == std::byte{'c'} && bytes[2] == std::byte{'e'});
  check(source->read_at(6U, bytes) == GNEISS_ERROR_INVALID_ARGUMENT);
  check(source->read_at(std::numeric_limits<std::uint64_t>::max(), bytes) ==
        GNEISS_ERROR_INVALID_ARGUMENT);
  check(source->read_at(8U, {}) == GNEISS_SUCCESS);
  const auto read = [&source](std::uint64_t offset, std::byte expected) {
    for (int iteration = 0; iteration < 100; ++iteration) {
      std::array<std::byte, 1> value{};
      if (source->read_at(offset, value) != GNEISS_SUCCESS || value[0] != expected) {
        return false;
      }
    }
    return true;
  };
  auto first = std::async(std::launch::async, read, 0U, std::byte{'a'});
  auto second = std::async(std::launch::async, read, 7U, std::byte{'h'});
  check(first.get() && second.get());
  // 长期来源不阻止文件重命名或同路径替换，也不能转而读取新版文件。
  std::filesystem::rename(directory.path / "data", directory.path / "previous");
  {
    std::ofstream replacement(directory.path / "data", std::ios::binary);
    replacement << "new-data";
  }
  check(source->read_at(0U, bytes) == GNEISS_SUCCESS && bytes[0] == std::byte{'a'});
  // 截断原文件并不修改打开时长度；读取缺失字节必须报告短读。
  std::filesystem::resize_file(directory.path / "previous", 2U);
  check(source->size() == 8U);
  check(source->read_at(4U, bytes) == GNEISS_ERROR_IO);
  check(source->read_at(0U, std::span{bytes}.first(1)) == GNEISS_SUCCESS);
  check(std::filesystem::remove(directory.path / "previous"));
  check(source->read_at(0U, std::span{bytes}.first(1)) == GNEISS_SUCCESS &&
        bytes[0] == std::byte{'a'});
}
} // namespace

int main() try {
  deferred_reads();
#ifdef _WIN32
  native_deferred_reads();
#endif
  run();
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
