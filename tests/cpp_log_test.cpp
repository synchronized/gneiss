// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/logging/detail/async_queue.hpp"
#include "engine/core/logging/validation.hpp"

#include <type_traits>

int main() {
  using gneiss::engine::error;
  constexpr gneiss::engine::logging::message_view message{
      .severity = gneiss::engine::logging::level::warning,
      .category = "game",
      .message = "状态变化",
      .result = error::not_ready};
  static_assert(message.severity == gneiss::engine::logging::level::warning);
  static_assert(message.result == error::not_ready);
  static_assert(message.category == "game");
  if (gneiss::engine::logging::validate_message(message).failed()) {
    return 1;
  }
  gneiss::engine::logging::message_view value{};
  if (value.severity != gneiss::engine::logging::level::info || value.result != error::success ||
      gneiss::engine::logging::validate_message(value) != error::invalid_argument) {
    return 2;
  }
  value.category = "test";
  if (gneiss::engine::logging::validate_message(value).failed()) {
    return 3;
  }
  value.severity = static_cast<gneiss::engine::logging::level>(0xffffffffU);
  if (gneiss::engine::logging::validate_message(value) != error::invalid_argument) {
    return 4;
  }
  value.severity = gneiss::engine::logging::level::info;
  value.message = "\xC0\x80";
  if (gneiss::engine::logging::validate_message(value) != error::invalid_argument) {
    return 5;
  }
  value.message = "有效文本";
  if (gneiss::engine::logging::validate_message(value).failed()) {
    return 6;
  }
  return 0;
}
