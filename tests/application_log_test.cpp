// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "application/application.hpp"
#include "support/logging.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
int main() try {
  namespace e = gneiss::engine;
  unsigned count{};
  bool valid = true;
  gneiss::test_support::log_capture logs{[&](const e::logging::record_view& event) {
    ++count;
    valid = valid && event.message.context == 123 && event.message.result == e::error::not_found &&
            event.message.source == "loader" && event.message.category == "asset" &&
            event.message.message == "missing";
  }};
  static const e::logging::logger log{"application"};
  // Application 创建前和销毁后，日志仍属于宿主服务。
  const auto emit = [&] {
    log.write({.context = 123,
               .severity = e::logging::level::warning,
               .source = "loader",
               .category = "asset",
               .message = "missing",
               .result = e::error::not_found});
  };
  emit();
  auto created = gneiss::runtime::application::create({});
  if (!created)
    return 1;
  auto application = std::move(*created);
  emit();
  if (application->shutdown().failed())
    return 2;
  application.reset();
  emit();
  logs.service.flush();
  return count == 3 && valid ? 0 : 3;
} catch (const std::exception& error) {
  std::cerr << error.what();
  return 4;
}
