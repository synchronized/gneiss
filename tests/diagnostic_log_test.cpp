// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime/application.hpp"
#include <memory>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>

namespace {

struct capture_state final {
  std::uint32_t diagnostic_count = 0U;
  std::uint32_t log_count = 0U;
  std::mutex mutex;
  std::condition_variable changed;
  std::string source;
  std::string category;
  gneiss::engine::error result;
};

} // namespace

int main() {
  capture_state state;
  gneiss::runtime::application_configuration config;
  config.callbacks.update = [](const auto&) { return gneiss::engine::error::internal; };
  config.callbacks.diagnostic = [&](const gneiss::engine::diagnostic_view& diagnostic) {
    if (diagnostic.result == gneiss::engine::error::internal) {
      ++state.diagnostic_count;
    }
  };
  config.callbacks.log = [&](const gneiss::engine::event_view& event) {
    {
      const std::scoped_lock lock(state.mutex);
      ++state.log_count;
      state.source = event.message.source;
      state.category = event.message.category;
      state.result = event.message.result;
    }
    state.changed.notify_all();
  };
  std::shared_ptr<gneiss::runtime::application> application;
  if (gneiss::runtime::application::create(config, application).failed() ||
      application->run(1U) != gneiss::engine::error::internal || state.diagnostic_count != 1U) {
    return 1;
  }
  {
    std::unique_lock lock(state.mutex);
    if (!state.changed.wait_for(lock, std::chrono::seconds(2),
                                [&state] { return state.log_count == 1U; }) ||
        state.source != "application" || state.category != "application" ||
        state.result != gneiss::engine::error::internal) {
      return 2;
    }
  }
  if (application->shutdown().failed()) {
    return 3;
  }
  return 0;
}
