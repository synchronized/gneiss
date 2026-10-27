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

namespace gneiss::engine::files {

/** 挂载来源的运行期标记；不拥有后端，不表示内容版本或持久身份。 */
struct source_domain final {};

struct request_id {
  std::uint64_t value{};
  friend bool operator==(request_id, request_id) = default;
};
using bytes = std::vector<std::byte>;
struct block {
  files::bytes bytes;
  std::uint64_t offset{};
  bool eof{};
};
enum class kind { regular, directory };
struct info {
  files::kind kind{files::kind::regular};
  std::optional<std::uint64_t> size;
};
struct read_options {
  std::size_t max_bytes{};
};
struct range {
  std::uint64_t offset{};
  std::size_t size{};
};
enum class write_mode { replace, create_new };
/** 单文件安全写入：完整准备后替换；create_new 不覆盖已有目标。
 * 成功表示后端可见；浏览器持久化另需 flush，不承诺跨文件原子性或目录崩溃耐久性。 */
struct write_options {
  write_mode mode{write_mode::replace};
};
struct directory_entry {
  std::string name;
  files::info info;
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
template <class T> struct completion {
  result status;
  T data{}; // 失败时为空，不提供部分结果。
};
template <class T> using callback = std::function<void(completion<T>)>;
using completion_callback = std::function<void(result)>;

} // namespace gneiss::engine::files
