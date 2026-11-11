// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/log/logging_runtime.hpp"
#include <iostream>
#include <stdexcept>
#include <thread>

struct counted_format {
  int* count;
};
template <> struct std::formatter<counted_format> : std::formatter<int> {
  auto format(const counted_format& value, std::format_context& context) const {
    ++*value.count;
    return std::formatter<int>::format(7, context);
  }
};
namespace {
namespace logging = gneiss::engine::logging;
namespace engine = gneiss::engine;
void require(bool condition) {
  if (!condition)
    throw std::runtime_error("logging runtime contract failed");
}
struct memory_sink final : logging::sink {
  std::vector<std::string> messages;
  std::vector<std::string> channels;
  std::uint64_t sequence{};
  int flushes{};
  logging::logger recursive;
  bool fail{};
  engine::error write(const engine::event_view& event) override {
    require(event.sequence > sequence && event.thread_id != 0);
    sequence = event.sequence;
    messages.emplace_back(event.message.message);
    channels.emplace_back(event.message.category);
    recursive.info("must not recurse");
    return fail ? engine::error::io : engine::error::success;
  }
  engine::error flush() override {
    ++flushes;
    return engine::error::success;
  }
};
void test() {
  auto output = std::make_unique<memory_sink>();
  auto* observed = output.get();
  std::vector<std::unique_ptr<logging::sink>> outputs;
  outputs.push_back(std::move(output));
  logging::runtime runtime{std::move(outputs)};
  auto log = runtime.get_logger("asset");
  observed->recursive = log;
  int formatted{};
  log.debug("{}", counted_format{&formatted});
  require(formatted == 0);
  runtime.set_level(engine::level::debug);
  log.debug("{}", counted_format{&formatted});
  require(formatted == 1);
  std::vector<std::thread> workers;
  for (int index = 0; index < 4; ++index)
    workers.emplace_back([log, index] {
      for (int value = 0; value < 20; ++value)
        log.info("{}:{}", index, value);
    });
  for (auto& worker : workers)
    worker.join();
  runtime.flush();
  require(observed->messages.size() == 81 && observed->messages.front() == "7");
  for (const auto& channel : observed->channels)
    require(channel == "asset");
  require(runtime.output_failures() == 0);
  observed->fail = true;
  log.error("output failure");
  runtime.flush();
  require(runtime.output_failures() == 1);
  runtime.close();
  require(!log.enabled(engine::level::info));
  log.info("after close {}", counted_format{&formatted});
  require(formatted == 1 && observed->messages.size() == 82);
  runtime.close();
  logging::logger expired;
  {
    std::vector<std::unique_ptr<logging::sink>> isolated;
    isolated.push_back(std::make_unique<memory_sink>());
    logging::runtime other{std::move(isolated)};
    expired = other.get_logger("isolated");
    expired.info("independent context");
    require(!log.enabled(engine::level::info));
  }
  require(!expired.enabled(engine::level::info));
  expired.info("expired");
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
