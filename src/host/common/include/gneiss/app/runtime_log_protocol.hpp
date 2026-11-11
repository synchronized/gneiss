// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/error.hpp"
#include "engine/core/logging/record.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gneiss::host {

inline constexpr std::string_view runtime_log_prefix = "@gneiss-log-v2 ";

enum class runtime_log_parse_result : std::uint8_t {
  success,
  not_protocol,
  unsupported_version,
  invalid,
};

struct runtime_log_record final {
  std::uint32_t version = 2U;
  std::uint32_t severity = static_cast<std::uint32_t>(gneiss::logging::level::info);
  std::uint64_t sequence = 0U;
  std::uint64_t timestamp_ns = 0U;
  std::uint64_t thread_id = 0U;
  std::uint64_t context = 0U;
  std::string source;
  std::string category;
  std::string message;
  gneiss::error operation = gneiss::error::success;
};

inline constexpr std::size_t maximum_runtime_log_line_size = 64U * 1024U;

struct runtime_log_line final {
  std::string text;
  bool was_truncated = false;
};

/**
 * 将任意分块的子进程字节流组装为日志行。
 *
 * 单行超过上限时保留前缀并丢弃该行剩余字节，下一行仍可继续解析。finish 用于进程退出时交付没有
 * 换行符的尾部。对象只应由单个消费线程使用。
 */
class runtime_log_line_decoder final {
public:
  explicit runtime_log_line_decoder(
      std::size_t maximum_line_size = maximum_runtime_log_line_size) noexcept;

  [[nodiscard]] gneiss::error append(std::string_view bytes, std::vector<runtime_log_line>& output);
  [[nodiscard]] gneiss::error finish(std::vector<runtime_log_line>& output);
  void reset() noexcept;

private:
  [[nodiscard]] gneiss::error emit(std::vector<runtime_log_line>& output);
  void append_segment(std::string_view segment);

  std::size_t maximum_line_size_;
  std::string pending_;
  bool pending_was_truncated_ = false;
};

/** 将日志事件编码为带版本前缀的单行 UTF-8 JSON，结果包含末尾换行。 */
[[nodiscard]] gneiss::error encode_runtime_log_event(const gneiss::logging::record_view& event,
                                                     std::string& output);

/** 解析一行 Runtime 日志协议；不属于协议的普通输出不会被视为错误。 */
[[nodiscard]] runtime_log_parse_result parse_runtime_log_line(std::string_view line,
                                                              runtime_log_record& output);

} // namespace gneiss::host
