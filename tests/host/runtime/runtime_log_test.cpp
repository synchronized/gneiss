// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include <gneiss/engine/core/logging/service.hpp>
#include "core/logging/file_sink.hpp"
#include "runtime_logging.hpp"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
int main() {
  const auto path = std::filesystem::temp_directory_path() / "gneiss-runtime-log-sink-test.log";
  std::error_code ec;
  std::filesystem::remove(path, ec);
  {
    gneiss::logging::service logs;
    auto file = gneiss::logging::create_rotating_file_sink(path);
    if (!file)
      return 1;
    logs.attach(std::move(*file));
    auto log = logs.get_logger("runtime");
    log.write({.context = 12,
               .severity = gneiss::logging::level::warning,
               .category = "startup",
               .message = "启动诊断",
               .result = gneiss::error::not_ready});
    logs.close();
    log.info("关闭后不应写入");
  }
  std::ifstream input(path);
  const std::string text{std::istreambuf_iterator<char>(input), {}};
  input.close();
  std::filesystem::remove(path, ec);
  return text.find("context=12") != std::string::npos &&
                 text.find("source=runtime category=startup") != std::string::npos &&
                 text.find("关闭后不应写入") == std::string::npos
             ? 0
             : 2;
}
