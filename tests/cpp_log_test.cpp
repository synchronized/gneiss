// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/log.hpp>

int main() {
  constexpr auto message =
      gneiss::make_log_message(gneiss::log_severity::warning, "game", "状态变化");
  static_assert(message.severity == GNEISS_LOG_WARNING);
  return gneiss::validate_log_message(message) == gneiss::result::success ? 0 : 1;
}
