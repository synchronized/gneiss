// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/log/log.hpp"
#include "engine/core/log/log_dispatcher.hpp"

#include <type_traits>

int main() {
  using gneiss::engine::result;
  constexpr gneiss::engine::message_view message{.severity = gneiss::engine::level::warning,
                                                 .category = "game",
                                                 .message = "状态变化",
                                                 .result = result::not_ready};
  static_assert(message.severity == gneiss::engine::level::warning);
  static_assert(message.result == result::not_ready);
  static_assert(message.category == "game");
  if (gneiss::engine::validate_message(message).failed()) {
    return 1;
  }
  gneiss::engine::message_view value{};
  if (value.severity != gneiss::engine::level::info || value.result != result::success ||
      gneiss::engine::validate_message(value) != result::invalid_argument) {
    return 2;
  }
  value.category = "test";
  if (gneiss::engine::validate_message(value).failed()) {
    return 3;
  }
  value.severity = static_cast<gneiss::engine::level>(0xffffffffU);
  if (gneiss::engine::validate_message(value) != result::invalid_argument) {
    return 4;
  }
  value.severity = gneiss::engine::level::info;
  value.message = "\xC0\x80";
  if (gneiss::engine::validate_message(value) != result::invalid_argument) {
    return 5;
  }
  value.message = "有效文本";
  if (gneiss::engine::validate_message(value).failed()) {
    return 6;
  }
  return 0;
}
