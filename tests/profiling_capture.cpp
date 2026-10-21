// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/tasks/task_scheduler.hpp"
#include "support/c_owner.hpp"
#include <gneiss/engine/application.h>
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
  gneiss::engine::core::tasks::task_scheduler scheduler{{.workers = 1U}};
  gneiss::engine::core::tasks::task_scope scope = scheduler.make_scope();
  std::size_t received{};
};
gneiss_result update(gneiss_application, const gneiss_frame_time* time, void* opaque) noexcept {
  auto& state = *static_cast<context*>(opaque);
  try {
    gneiss::engine::core::tasks::task_handle task;
    const auto accepted = state.scheduler.submit(
        {.name = time->frame_index == 0U ? std::string(65536U, 'x') : "capture.work",
         .scope = state.scope},
        [](const gneiss::engine::core::tasks::task_context&) {
          std::this_thread::sleep_for(1ms);
          return gneiss::engine::core::tasks::task_outcome{
              .state = gneiss::engine::core::tasks::task_state::succeeded};
        },
        task);
    if (accepted != gneiss::engine::core::tasks::submit_result::success) {
      return GNEISS_ERROR_INTERNAL;
    }
    std::vector<gneiss::engine::core::tasks::task_completion> completed;
    state.received += state.scheduler.poll(state.scope, completed);
    std::this_thread::sleep_for(2ms);
    return GNEISS_SUCCESS;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
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
  gneiss_application_desc description = GNEISS_APPLICATION_DESC_INIT;
  description.user_data = &state;
  description.update = update;
  gneiss::test_support::application_owner application;
  if (gneiss_application_create(&description, application.put()) != GNEISS_SUCCESS ||
      gneiss_application_run(application.get(), 128) != GNEISS_SUCCESS) {
    return 3;
  }
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (state.received < 128U && std::chrono::steady_clock::now() < deadline) {
    std::vector<gneiss::engine::core::tasks::task_completion> completed;
    state.received += state.scheduler.poll(state.scope, completed);
    std::this_thread::sleep_for(1ms);
  }
  std::printf("received=%zu\n", state.received);
  return state.received == 128U ? 0 : 4;
} catch (...) {
  return 1;
}
