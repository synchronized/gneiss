// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "uv_file_reader.hpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <future>
#include <limits>
#include <source_location>
#include <stdexcept>
#include <string>

namespace {
using gneiss::result;
using gneiss::engine::platform::io::uv_file_reader;
void check(bool value, std::source_location where = std::source_location::current()) {
  if (!value) {
    throw std::runtime_error("libuv 文件来源测试失败，行=" + std::to_string(where.line()));
  }
}
struct directory final {
  std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      ("gneiss-uv-read-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  directory() { check(std::filesystem::create_directory(path)); }
  ~directory() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};
std::string utf8(const std::filesystem::path& path) {
  const auto value = path.u8string();
  return {value.begin(), value.end()};
}
void run() {
  directory temporary;
  const auto path = temporary.path / std::filesystem::path(u8"读取.bin");
  {
    std::ofstream file(path, std::ios::binary);
    file << "abcdefgh";
  }
  std::unique_ptr<uv_file_reader> reader;
  check(uv_file_reader::open("", reader) == result::invalid_argument && !reader);
  check(uv_file_reader::open(std::string("bad\0path", 8U), reader) == result::invalid_argument);
  check(uv_file_reader::open(utf8(temporary.path / "missing"), reader) == result::not_found);
  check(uv_file_reader::open(utf8(temporary.path), reader) != result::success && !reader);
  check(uv_file_reader::open(utf8(path), reader) == result::success && reader->size() == 8U);
  check(reader->read_at(8U, {}) == result::success);
  check(reader->read_at(9U, {}) == result::invalid_argument);
  std::array<std::byte, 3> bytes{};
  check(reader->read_at(2U, bytes) == result::success && bytes[0] == std::byte{'c'} &&
        bytes[2] == std::byte{'e'});
  check(reader->read_at(std::numeric_limits<std::uint64_t>::max(), bytes) ==
        result::invalid_argument);
  check(reader->read_at(7U, bytes) == result::invalid_argument);
  const auto read = [&reader](std::uint64_t offset, std::byte expected) {
    for (unsigned i = 0; i < 100U; ++i) {
      std::array<std::byte, 1> value{};
      if (reader->read_at(offset, value) != result::success || value[0] != expected) {
        return false;
      }
    }
    return true;
  };
  auto first = std::async(std::launch::async, read, 0U, std::byte{'a'});
  auto second = std::async(std::launch::async, read, 7U, std::byte{'h'});
  check(first.get() && second.get());
  const auto previous = temporary.path / "previous";
  std::filesystem::rename(path, previous);
  {
    std::ofstream replacement(path, std::ios::binary);
    replacement << "replacement";
  }
  check(reader->read_at(0U, bytes) == result::success && bytes[0] == std::byte{'a'});
  std::filesystem::resize_file(previous, 2U);
  std::uint64_t current{};
  check(reader->current_size(current) == result::success && current == 2U && reader->size() == 8U);
  check(reader->read_at(1U, bytes) == result::io);
  std::filesystem::remove(previous);
  check(reader->read_at(0U, std::span{bytes}.first(1U)) == result::success);
  check(uv_file_reader::open(utf8(temporary.path / "missing"), reader) == result::not_found &&
        !reader);
  const auto empty = temporary.path / "empty";
  {
    std::ofstream file(empty, std::ios::binary);
  }
  check(uv_file_reader::open(utf8(empty), reader) == result::success && reader->size() == 0U);
  check(reader->read_at(0U, {}) == result::success);
}
} // namespace

int main() try {
  run();
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
