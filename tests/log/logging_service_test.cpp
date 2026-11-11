// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/logging/default_service.hpp"
#include "engine/core/logging/service.hpp"
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
  engine::error write(const engine::logging::record_view& event) override {
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
static const logging::logger static_log{"static"};
void test_default_and_sync() {
  require(!static_log.enabled(engine::logging::level::info));
  logging::service logs{{.mode = logging::dispatch_mode::synchronous}};
  auto output = std::make_unique<memory_sink>();
  auto* memory = output.get();
  logging::scoped_sink_connection connection =
      logs.attach(std::move(output), {.minimum_level = engine::logging::level::warning});
  logging::default_service_binding binding{logs};
  bool duplicate{};
  try {
    logging::default_service_binding invalid{logs};
  } catch (const std::logic_error&) {
    duplicate = true;
  }
  require(duplicate);
  require(!static_log.enabled(engine::logging::level::info));
  static_log.warning("synchronous");
  require(memory->messages.size() == 1 && memory->channels.front() == "static");
  connection.set_level(engine::logging::level::trace);
  static_log.info("visible");
  require(memory->messages.size() == 2);
  binding.reset();
  require(!static_log.enabled(engine::logging::level::warning));
  {
    logging::default_service_binding rebound{logs};
    static_log.info("rebound");
  }
  require(memory->messages.size() == 3);
  connection.disconnect();
  require(!connection.connected());
  require(!logs.get_logger("isolated").enabled(engine::logging::level::fatal));
}
void test() {
  auto output = std::make_unique<memory_sink>();
  auto* observed = output.get();
  std::vector<std::unique_ptr<logging::sink>> outputs;
  outputs.push_back(std::move(output));
  logging::service runtime;
  auto connection = runtime.attach(std::move(outputs.front()));
  auto log = runtime.get_logger("asset");
  observed->recursive = log;
  int formatted{};
  log.debug("{}", counted_format{&formatted});
  require(formatted == 0);
  runtime.set_level(engine::logging::level::debug);
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
  require(runtime.metrics().output_failures == 0);
  observed->fail = true;
  log.error("output failure");
  runtime.flush();
  require(runtime.metrics().output_failures == 1);
  require(observed->messages.size() == 82);
  runtime.close();
  require(!connection.connected());
  require(!log.enabled(engine::logging::level::info));
  log.info("after close {}", counted_format{&formatted});
  require(formatted == 1);
  runtime.close();
  logging::logger expired;
  {
    std::vector<std::unique_ptr<logging::sink>> isolated;
    isolated.push_back(std::make_unique<memory_sink>());
    logging::service other;
    other.attach(std::move(isolated.front()));
    expired = other.get_logger("isolated");
    expired.info("independent context");
    require(!log.enabled(engine::logging::level::info));
  }
  require(!expired.enabled(engine::logging::level::info));
  expired.info("expired");
}
}
int main() {
  try {
    test();
    test_default_and_sync();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
