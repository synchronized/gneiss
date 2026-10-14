// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/tasks/task_scheduler.hpp"
#include <gneiss/engine/application.hpp>
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
  gneiss::tasks::task_scheduler scheduler{{.workers = 1U}};
  gneiss::tasks::task_scope scope = scheduler.make_scope();
  std::size_t received{};
};
gneiss::result update(gneiss::application_ref /*unused*/, const gneiss::frame_time& time,
                      void* opaque) noexcept {
  auto& state = *static_cast<context*>(opaque);
  try {
    gneiss::tasks::task_handle task;
    const auto accepted = state.scheduler.submit(
        {.name = time.frame_index == 0U ? std::string(65536U, 'x') : "capture.work",
         .scope = state.scope},
        [](const gneiss::tasks::task_context&) {
          std::this_thread::sleep_for(1ms);
          return gneiss::tasks::task_outcome{.state = gneiss::tasks::task_state::succeeded};
        },
        task);
    if (accepted != gneiss::tasks::submit_result::success) {
      return gneiss::result::internal;
    }
    std::vector<gneiss::tasks::task_completion> completed;
    state.received += state.scheduler.poll(state.scope, completed);
    std::this_thread::sleep_for(2ms);
    return gneiss::result::success;
  } catch (...) {
    return gneiss::result::internal;
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
  gneiss::application_desc description{.platform = gneiss::application_platform::callback};
  description.callbacks.user_data = &state;
  description.callbacks.update = update;
  gneiss::application application;
  if (gneiss::application::create(description, application).failed() ||
      application.run(128).failed()) {
    return 3;
  }
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (state.received < 128U && std::chrono::steady_clock::now() < deadline) {
    std::vector<gneiss::tasks::task_completion> completed;
    state.received += state.scheduler.poll(state.scope, completed);
    std::this_thread::sleep_for(1ms);
  }
  std::printf("received=%zu\n", state.received);
  return state.received == 128U ? 0 : 4;
} catch (...) {
  return 1;
}
