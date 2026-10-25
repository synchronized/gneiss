// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/result.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace gneiss::engine {

struct file_request_id {
  std::uint64_t value{};
  friend bool operator==(file_request_id, file_request_id) = default;
};
using file_bytes = std::vector<std::byte>;
enum class file_kind { regular, directory };
struct file_info {
  file_kind kind{file_kind::regular};
  std::optional<std::uint64_t> size;
};
struct read_options {
  std::size_t max_bytes{};
};
struct file_range {
  std::uint64_t offset{};
  std::size_t size{};
};
enum class write_mode { replace, create_new };
struct write_options {
  write_mode mode{write_mode::replace};
};
struct directory_entry {
  std::string name;
  file_info info;
};
/** 游标为后端返回的不透明值，只能用于同一路径和后端；不保证目录快照。 */
struct list_options {
  std::size_t max_entries{128};
  std::string cursor;
};
struct directory_page {
  std::vector<directory_entry> entries;
  std::string next_cursor;
};
template <class T> struct file_completion {
  result status;
  T data{}; // 失败时为空，不提供部分结果。
};
template <class T> using file_callback = std::function<void(file_completion<T>)>;
using completion_callback = std::function<void(result)>;

} // namespace gneiss::engine
