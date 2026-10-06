// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/log.hpp>

#include <type_traits>

int main() {
  using gneiss::result;
  constexpr auto message = gneiss::make_log_message(gneiss::log_severity::warning, "game",
                                                    "状态变化", result::not_ready);
  static_assert(!std::is_same_v<decltype(message), const gneiss_log_message>);
  static_assert(message.severity == gneiss::log_severity::warning);
  static_assert(message.operation == result::not_ready);
  static_assert(message.category == "game");
  if (gneiss::validate_log_message(message).failed()) {
    return 1;
  }
  gneiss::log_message value{};
  if (value.severity != gneiss::log_severity::info || value.operation != result::success ||
      gneiss::validate_log_message(value) != result::invalid_argument) {
    return 2;
  }
  value.category = "test";
  if (gneiss::validate_log_message(value).failed()) {
    return 3;
  }
  value.severity = static_cast<gneiss::log_severity>(0xffffffffU);
  if (gneiss::validate_log_message(value) != result::invalid_argument) {
    return 4;
  }
  value.severity = gneiss::log_severity::info;
  value.message = "\xC0\x80";
  if (gneiss::validate_log_message(value) != result::invalid_argument) {
    return 5;
  }
  value.message = "有效文本";
  if (gneiss::validate_log_message(value).failed()) {
    return 6;
  }
  // 显式转换只借用，不补建字符串缓存或改变结果码。
  const auto native = gneiss::to_native(message);
  if (native.category != message.category.data() || native.message != message.message.data() ||
      native.result != message.operation.native() || native.flags != 0U ||
      native.reserved[0] != 0U || native.reserved[1] != 0U) {
    return 7;
  }
  return 0;
}
