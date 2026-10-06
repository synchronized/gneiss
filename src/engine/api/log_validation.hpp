// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SRC_API_C_LOG_VALIDATION_HPP
#define GNEISS_SRC_API_C_LOG_VALIDATION_HPP

#include "engine/core/log/log.hpp"
#include "engine/core/log/log_dispatcher.hpp"
#include <gneiss/engine/log.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string_view>

namespace gneiss::abi_internal {
/** 仅用于通过布局校验后的描述；文本借用到同步提交返回。 */
[[nodiscard]] inline log_internal::message_view
log_message_view(const gneiss_log_message& message) noexcept {
  return {
      .severity = message.severity,
      .category =
          std::string_view(message.category, static_cast<std::size_t>(message.category_length)),
      .message =
          message.message_length == 0U
              ? std::string_view{}
              : std::string_view(message.message, static_cast<std::size_t>(message.message_length)),
      .result = message.result,
  };
}

/** 多个 ABI 入口共用布局校验；不经导出函数复用核心规则。 */
[[nodiscard]] inline gneiss_result
validate_log_message(const gneiss_log_message* message) noexcept {
  if (message == nullptr || message->struct_size < GNEISS_LOG_MESSAGE_VERSION_1_SIZE ||
      message->severity < GNEISS_LOG_TRACE || message->severity > GNEISS_LOG_FATAL ||
      message->category == nullptr || message->category_length == 0U || message->flags != 0U ||
      (message->message == nullptr && message->message_length != 0U) ||
      message->category_length > std::numeric_limits<std::size_t>::max() ||
      message->message_length > std::numeric_limits<std::size_t>::max() ||
      std::ranges::any_of(message->reserved, [](std::uint64_t value) { return value != 0U; })) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  const std::string_view category(message->category,
                                  static_cast<std::size_t>(message->category_length));
  const auto text =
      message->message_length == 0U
          ? std::string_view{}
          : std::string_view(message->message, static_cast<std::size_t>(message->message_length));
  return log_internal::valid_text(category, text) ? GNEISS_SUCCESS : GNEISS_ERROR_INVALID_ARGUMENT;
}
} // namespace gneiss::abi_internal

#endif
