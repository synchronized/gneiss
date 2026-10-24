// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "engine/core/tasks/task_scheduler.hpp"
#include "runtime/application.hpp"
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
  gneiss::engine::task_scheduler scheduler{{.workers = 1U}};
  gneiss::engine::task_scope scope = scheduler.make_scope();
  std::size_t received{};
};
gneiss::engine::result update(const gneiss::engine::frame_time& time, context& state) noexcept {
  try {
    gneiss::engine::task_handle task;
    const auto accepted = state.scheduler.submit(
        {.name = time.frame_index == 0U ? std::string(65536U, 'x') : "capture.work",
         .scope = state.scope},
        [](const gneiss::engine::task_context&) {
          std::this_thread::sleep_for(1ms);
          return gneiss::engine::task_outcome{.state = gneiss::engine::task_state::succeeded};
        },
        task);
    if (accepted != gneiss::engine::submit_result::success) {
      return gneiss::engine::result::internal;
    }
    std::vector<gneiss::engine::task_completion> completed;
    state.received += state.scheduler.poll(state.scope, completed);
    std::this_thread::sleep_for(2ms);
    return gneiss::engine::result::success;
  } catch (...) {
    return gneiss::engine::result::internal;
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
  description.callbacks.update = [&](const auto& time) { return update(time, state); };
  std::shared_ptr<gneiss::runtime::application> application;
  if (gneiss::runtime::application::create(description, application).failed() ||
      application->run(128).failed()) {
    return 3;
  }
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (state.received < 128U && std::chrono::steady_clock::now() < deadline) {
    std::vector<gneiss::engine::task_completion> completed;
    state.received += state.scheduler.poll(state.scope, completed);
    std::this_thread::sleep_for(1ms);
  }
  std::printf("received=%zu\n", state.received);
  return state.received == 128U ? 0 : 4;
} catch (...) {
  return 1;
}
