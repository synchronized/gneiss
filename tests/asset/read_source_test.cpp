// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/asset/native_file_system.hpp"
#include "engine/asset/virtual_file_system.hpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <future>
#include <limits>
#include <source_location>
#include <stdexcept>

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
  run();
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
