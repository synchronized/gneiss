// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_LOG_HPP_
#define GNEISS_LOG_HPP_

#include <gneiss/core/result.hpp>
#include <gneiss/log.h>

#include <cstdint>
#include <string_view>

namespace gneiss {

enum class log_severity : std::uint32_t {
  trace = GNEISS_LOG_TRACE,
  debug = GNEISS_LOG_DEBUG,
  info = GNEISS_LOG_INFO,
  warning = GNEISS_LOG_WARNING,
  error = GNEISS_LOG_ERROR,
  fatal = GNEISS_LOG_FATAL,
};

/** 构造借用字符串的原生日志消息；返回值不得比 category 和 message 存活更久。 */
[[nodiscard]] constexpr gneiss_log_message
make_log_message(log_severity severity, std::string_view category, std::string_view message,
                 result operation = result::success) noexcept {
  return {.struct_size = sizeof(gneiss_log_message),
          .severity = static_cast<std::uint32_t>(severity),
          .category = category.data(),
          .category_length = category.size(),
          .message = message.data(),
          .message_length = message.size(),
          .result = to_native(operation),
          .flags = 0U,
          .reserved = {0U, 0U}};
}

/** 校验日志消息，不取得字符串所有权。 */
[[nodiscard]] inline result validate_log_message(const gneiss_log_message& message) noexcept {
  return from_native(gneiss_log_message_validate(&message));
}

} // namespace gneiss

#endif
