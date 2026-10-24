// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "runtime_log.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using namespace gneiss::host;

int main() {
  const auto path = std::filesystem::temp_directory_path() / "gneiss-runtime-log-sink-test.log";
  std::error_code error;
  std::filesystem::remove(path, error);
  {
    gneiss::host::runtime_log log(path);
    constexpr std::string_view source = "application";
    constexpr std::string_view category = "test";
    constexpr std::string_view message = "line 1\n\"line 2\"";
    const gneiss::engine::event_view event{
        .message = {.severity = gneiss::engine::level::warning,
                    .source = source,
                    .category = category,
                    .message = message,
                    .result = gneiss::engine::result::not_ready},
        .sequence = 7U,
        .timestamp_ns = 42U,
        .thread_id = 3U,
    };
    log.write(event);
  }
  std::ifstream input(path);
  const std::string content{std::istreambuf_iterator<char>(input),
                            std::istreambuf_iterator<char>()};
  std::filesystem::remove(path, error);
  return content.find("time_ns=42 sequence=7 level=WARNING process=runtime") != std::string::npos &&
                 content.find("source=\"application\" category=\"test\" thread=3") !=
                     std::string::npos &&
                 content.find("message=\"line 1\\n\\\"line 2\\\"\"") != std::string::npos
             ? 0
             : 1;
}
