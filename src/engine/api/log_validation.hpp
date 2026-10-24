// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SRC_API_C_LOG_VALIDATION_HPP
#define GNEISS_SRC_API_C_LOG_VALIDATION_HPP

#include "engine/api/result_conversion.hpp"
#include "engine/core/log/log.hpp"
#include "engine/core/log/log_dispatcher.hpp"
#include "engine/core/result.hpp"
#include <gneiss/engine/log.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string_view>

namespace gneiss::engine::detail {
static_assert(static_cast<std::uint32_t>(gneiss::engine::level::trace) == GNEISS_LOG_TRACE);
static_assert(static_cast<std::uint32_t>(gneiss::engine::level::debug) == GNEISS_LOG_DEBUG);
static_assert(static_cast<std::uint32_t>(gneiss::engine::level::info) == GNEISS_LOG_INFO);
static_assert(static_cast<std::uint32_t>(gneiss::engine::level::warning) == GNEISS_LOG_WARNING);
static_assert(static_cast<std::uint32_t>(gneiss::engine::level::error) == GNEISS_LOG_ERROR);
static_assert(static_cast<std::uint32_t>(gneiss::engine::level::fatal) == GNEISS_LOG_FATAL);
/** 仅用于通过布局校验后的描述；文本借用到同步提交返回。 */
[[nodiscard]] inline gneiss::engine::message_view
log_message_view(const gneiss_log_message& message) noexcept {
  return {
      .severity = static_cast<gneiss::engine::level>(message.severity),
      .category =
          std::string_view(message.category, static_cast<std::size_t>(message.category_length)),
      .message =
          message.message_length == 0U
              ? std::string_view{}
              : std::string_view(message.message, static_cast<std::size_t>(message.message_length)),
      .result = from_c_result(message.result),
  };
}

/** 多个 ABI 入口共用布局校验；不经导出函数复用核心规则。 */
[[nodiscard]] inline gneiss::engine::result
validate_log_message(const gneiss_log_message* message) noexcept {
  if (message == nullptr || message->struct_size < GNEISS_LOG_MESSAGE_VERSION_1_SIZE ||
      message->severity < GNEISS_LOG_TRACE || message->severity > GNEISS_LOG_FATAL ||
      message->category == nullptr || message->category_length == 0U || message->flags != 0U ||
      (message->message == nullptr && message->message_length != 0U) ||
      message->category_length > std::numeric_limits<std::size_t>::max() ||
      message->message_length > std::numeric_limits<std::size_t>::max() ||
      std::ranges::any_of(message->reserved, [](std::uint64_t value) { return value != 0U; })) {
    return gneiss::engine::result::invalid_argument;
  }
  const std::string_view category(message->category,
                                  static_cast<std::size_t>(message->category_length));
  const auto text =
      message->message_length == 0U
          ? std::string_view{}
          : std::string_view(message->message, static_cast<std::size_t>(message->message_length));
  return gneiss::engine::valid_text(category, text) ? gneiss::engine::result::success
                                                    : gneiss::engine::result::invalid_argument;
}
} // namespace gneiss::engine::detail

#endif
