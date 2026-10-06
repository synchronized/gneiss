// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_LOG_HPP_
#define GNEISS_LOG_HPP_

#include <gneiss/engine/core/result.hpp>
#include <gneiss/engine/log.h>

#include <cstdint>
#include <string_view>

namespace gneiss {

// NOLINTNEXTLINE(performance-enum-size): 与 C 协议同宽，非法扩展值交由校验拒绝，不能截断。
enum class log_severity : std::uint32_t {
  trace = GNEISS_LOG_TRACE,
  debug = GNEISS_LOG_DEBUG,
  info = GNEISS_LOG_INFO,
  warning = GNEISS_LOG_WARNING,
  error = GNEISS_LOG_ERROR,
  fatal = GNEISS_LOG_FATAL,
};

/** 字符串只在日志消费线程的当次回调内借用。 */
struct log_event {
  log_severity severity = log_severity::info;
  std::uint64_t sequence{}, timestamp_ns{}, thread_id{};
  std::string_view source, category, message;
  result operation = result::success;
};

/** 日志提交值；字符串由调用方持有至提交返回，复制本结构不会复制文本。
 * 默认级别为 info，category 必须填写；线程安全取决于借用文本是否被并发修改。 */
struct log_message {
  log_severity severity = log_severity::info;
  std::string_view category;
  std::string_view message;
  result operation = result::success;
};

/** 构造借用字符串的消息；返回值不得比 category 和 message 存活更久。 */
[[nodiscard]] constexpr log_message make_log_message(log_severity severity,
                                                     std::string_view category,
                                                     std::string_view message,
                                                     result operation = result::success) noexcept {
  return {.severity = severity, .category = category, .message = message, .operation = operation};
}

/** 显式 C ABI 互操作；返回结构仍借用输入字符串，不转移或延长其寿命。 */
[[nodiscard]] constexpr gneiss_log_message to_native(const log_message& message) noexcept {
  return {
      .struct_size = sizeof(gneiss_log_message),
      .severity = static_cast<std::uint32_t>(message.severity),
      .category = message.category.data(),
      .category_length = message.category.size(),
      .message = message.message.data(),
      .message_length = message.message.size(),
      .result = to_native(message.operation),
      .flags = 0U,
      .reserved = {0U, 0U},
  };
}

/** 校验级别、分类和 UTF-8 文本，不取得字符串所有权；非法枚举值返回 invalid_argument。 */
[[nodiscard]] inline result validate_log_message(const log_message& message) noexcept {
  const auto native = to_native(message);
  return from_native(gneiss_log_message_validate(&native));
}

} // namespace gneiss

#endif
