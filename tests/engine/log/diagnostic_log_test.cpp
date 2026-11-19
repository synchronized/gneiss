// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../../support/logging.hpp"
#include "../../support/value_result.hpp"
#include <gneiss/engine/function/application/application.hpp>
#include <memory>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>

namespace {

struct capture_state final {
  std::uint32_t log_count = 0U;
  std::mutex mutex;
  std::condition_variable changed;
  std::string source;
  std::string category;
  gneiss::error result;
};

} // namespace

int main() {
  capture_state state;
  gneiss::app::configuration config;
  gneiss::test_support::log_capture logs{[&](const gneiss::logging::record_view& event) {
    {
      const std::scoped_lock lock(state.mutex);
      ++state.log_count;
      state.source = event.message.source;
      state.category = event.message.category;
      state.result = event.message.result;
    }
    state.changed.notify_all();
  }};
  std::shared_ptr<gneiss::app::application> application;
  if (gneiss::test_support::assign_value(gneiss::app::application::create(config), application)
          .failed()) {
    return 1;
  }
  application->add_system(gneiss::app::stage::update, "test.failure",
                          [] { return gneiss::error::internal; });
  if (application->run().error() != gneiss::error::internal)
    return 1;
  {
    std::unique_lock lock(state.mutex);
    if (!state.changed.wait_for(lock, std::chrono::seconds(2),
                                [&state] { return state.log_count == 1U; }) ||
        state.source != "test.failure" || state.category != "application" ||
        state.result != gneiss::error::internal) {
      return 2;
    }
  }
  if (application->step().error() != gneiss::error::internal) {
    return 3;
  }
  logs.service.flush();
  const std::scoped_lock lock(state.mutex);
  return state.log_count == 1U ? 0 : 4;
}
