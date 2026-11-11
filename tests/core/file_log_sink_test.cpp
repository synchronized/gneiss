// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/platform/logging/file_sink.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
void require(bool value) {
  if (!value)
    throw std::runtime_error("file sink contract failed");
}
void test() {
  namespace logging = gneiss::engine::logging;
  namespace engine = gneiss::engine;
  const auto path =
      std::filesystem::temp_directory_path() /
      ("gneiss-log-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directory(path);
  struct cleanup {
    std::filesystem::path path;
    ~cleanup() {
      std::error_code error;
      std::filesystem::remove_all(path, error);
    }
  } cleanup{path};
  const auto file = path / "record.log";
  auto opened = logging::create_file_sink(file);
  require(opened.ok());
  const engine::logging::record_view event{.message = {.context = 42,
                                                       .severity = engine::logging::level::warning,
                                                       .source = "asset",
                                                       .category = "load",
                                                       .message = "literal {braces}",
                                                       .result = engine::error::not_found},
                                           .sequence = 17,
                                           .timestamp_ns = 123,
                                           .thread_id = 999};
  require((*opened)->write(event).ok());
  require((*opened)->flush().ok());
  opened->reset();
  {
    std::ifstream input(file);
    const std::string text{std::istreambuf_iterator<char>{input}, {}};
    require(text == "[time_ns=123 sequence=17 thread=999 context=42 level=4 source=asset "
                    "category=load result=-11] literal {braces}\n");
  }
  auto append = logging::create_file_sink(file);
  require(append.ok() && (*append)->write(event).ok() && (*append)->flush().ok());
  append->reset();
  {
    std::ifstream input(file);
    std::string line;
    require(static_cast<bool>(std::getline(input, line)));
    require(static_cast<bool>(std::getline(input, line)));
  }
  auto failed = logging::create_file_sink(path);
  require(failed.failed() && failed.error() == engine::error::io);
  require(logging::create_file_sink({}).failed());
  auto truncated = logging::create_file_sink(file, true);
  require(truncated.ok());
  auto invalid = event;
  invalid.message.severity = static_cast<engine::logging::level>(0);
  require((*truncated)->write(invalid) == engine::error::invalid_argument);
  truncated->reset();
  require(std::filesystem::file_size(file) == 0);
}
}
int main() {
  try {
    test();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
