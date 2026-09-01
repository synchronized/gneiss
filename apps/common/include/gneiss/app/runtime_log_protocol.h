// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <gneiss/core/result.hpp>
#include <gneiss/log.h>

#include <cstdint>
#include <string>
#include <string_view>

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

/** 将日志事件编码为带版本前缀的单行 UTF-8 JSON，结果包含末尾换行。 */
[[nodiscard]] result encode_runtime_log_event(const gneiss_log_event& event,
                                              std::string& output) noexcept;

/** 解析一行 Runtime 日志协议；不属于协议的普通输出不会被视为错误。 */
[[nodiscard]] runtime_log_parse_result parse_runtime_log_line(std::string_view line,
                                                              runtime_log_record& output) noexcept;

} // namespace gneiss::app
