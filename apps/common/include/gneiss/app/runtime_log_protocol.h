// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <gneiss/core/result.hpp>
#include <gneiss/log.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gneiss::app {

inline constexpr std::string_view runtime_log_prefix = "@gneiss-log-v1 ";

enum class runtime_log_parse_result : std::uint8_t {
  success,
  not_protocol,
  unsupported_version,
  invalid,
};

struct runtime_log_record final {
  std::uint32_t version = 1U;
  std::uint32_t severity = GNEISS_LOG_INFO;
  std::uint64_t sequence = 0U;
  std::uint64_t timestamp_ns = 0U;
  std::uint64_t thread_id = 0U;
  std::string source;
  std::string category;
  std::string message;
  gneiss_result operation = GNEISS_SUCCESS;
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

  [[nodiscard]] result append(std::string_view bytes,
                              std::vector<runtime_log_line>& output) noexcept;
  [[nodiscard]] result finish(std::vector<runtime_log_line>& output) noexcept;
  void reset() noexcept;

private:
  [[nodiscard]] result emit(std::vector<runtime_log_line>& output) noexcept;
  void append_segment(std::string_view segment);

  std::size_t maximum_line_size_;
  std::string pending_;
  bool pending_was_truncated_ = false;
};

/** 将日志事件编码为带版本前缀的单行 UTF-8 JSON，结果包含末尾换行。 */
[[nodiscard]] result encode_runtime_log_event(const gneiss_log_event& event,
                                              std::string& output) noexcept;

/** 解析一行 Runtime 日志协议；不属于协议的普通输出不会被视为错误。 */
[[nodiscard]] runtime_log_parse_result parse_runtime_log_line(std::string_view line,
                                                              runtime_log_record& output) noexcept;

} // namespace gneiss::app
