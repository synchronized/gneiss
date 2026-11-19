// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../../support/value_result.hpp"
#include <gneiss/engine/core/base/error.hpp>
#include <gneiss/engine/core/tasks/scheduler.hpp>
#include <gneiss/engine/function/application/application.hpp>
#include <tracy/Tracy.hpp>

#include <chrono>
#include <cstdio>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {
using namespace std::chrono_literals;
struct context {
  gneiss::tasks::scheduler scheduler{{.workers = 1U}};
  gneiss::tasks::scope scope = scheduler.make_scope();
  std::size_t received{};
};
gneiss::error update(const gneiss::frame_time& time, context& state) noexcept {
  try {
    gneiss::tasks::handle task;
    const auto accepted = state.scheduler.submit(
        {.name = time.frame_index == 0U ? std::string(65536U, 'x') : "capture.work",
         .scope = state.scope},
        [](const gneiss::tasks::context&) {
          std::this_thread::sleep_for(1ms);
          return gneiss::tasks::outcome{.state = gneiss::tasks::state::succeeded};
        },
        task);
    if (accepted != gneiss::tasks::submit_result::success) {
      return gneiss::error::internal;
    }
    std::vector<gneiss::tasks::completion> completed;
    state.received += state.scheduler.poll(state.scope, completed);
    std::this_thread::sleep_for(2ms);
    return gneiss::error::success;
  } catch (...) {
    return gneiss::error::internal;
  }
}
}
int main(int argc, char** argv) try {
  // 仅采集夹具主动等待连接，正式应用不等待分析器启动。
  if (argc == 2 && std::string_view(argv[1]) == "--wait-for-profiler") {
    const auto deadline = std::chrono::steady_clock::now() + 20s;
    while (!TracyIsConnected && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(10ms);
    }
    if (!TracyIsConnected) {
      return 2;
    }
  }
  context state;
  gneiss::runtime::application_configuration description;
  std::shared_ptr<gneiss::runtime::application> application;
  if (gneiss::test_support::assign_value(gneiss::runtime::application::create(description),
                                         application)
          .failed()) {
    return 3;
  }
  application->add_system(gneiss::app::stage::update, "profiling.update",
                          [&](const auto& time) { return update(time, state); });
  for (unsigned frame = 0; frame < 128; ++frame)
    if (!application->step())
      return 3;
  application->request_exit();
  if (!application->run())
    return 3;
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (state.received < 128U && std::chrono::steady_clock::now() < deadline) {
    std::vector<gneiss::tasks::completion> completed;
    state.received += state.scheduler.poll(state.scope, completed);
    std::this_thread::sleep_for(1ms);
  }
  std::printf("received=%zu\n", state.received);
  return state.received == 128U ? 0 : 4;
} catch (...) {
  return 1;
}
