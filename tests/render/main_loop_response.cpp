// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/application/application_asset_reload_internal.hpp"

#include <gneiss/engine/application.hpp>
#include <gneiss/engine/input.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {
using clock_type = std::chrono::steady_clock;
using namespace std::chrono_literals;
std::int64_t now_ns() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(clock_type::now().time_since_epoch())
      .count();
}
struct response_context {
  gneiss::tasks::task_scheduler scheduler{{.workers = 1U}};
  gneiss::tasks::task_scope scope = scheduler.make_scope();
  std::atomic_int64_t key_sent, task_finished, close_sent;
  std::atomic_uint sent, keys, tasks;
  std::atomic_bool failed, stopped;
  std::mutex mutex;
  std::condition_variable received;
  std::vector<double> key_ms, task_ms;
  double close_ms{};
  bool stall{};
  const clock_type::time_point deadline = clock_type::now() + 10s;
};
gneiss_result update(gneiss_application app, const gneiss_frame_time* /*unused*/,
                     void* opaque) try {
  auto& state = *static_cast<response_context*>(opaque);
  if (state.stall && state.sent.load() == 1U && state.keys.load() == 0U) {
    std::this_thread::sleep_for(50ms);
    state.stall = false;
  }
  gneiss_input_event event = GNEISS_INPUT_EVENT_INIT;
  while (gneiss_application_poll_input(app, &event) == GNEISS_SUCCESS) {
    if (event.type == GNEISS_INPUT_EVENT_KEY &&
        event.data.key.physical_key == GNEISS_PHYSICAL_KEY_A &&
        event.data.key.action == GNEISS_KEY_PRESSED) {
      state.key_ms.push_back(static_cast<double>(now_ns() - state.key_sent.load()) / 1e6);
      std::scoped_lock lock(state.mutex);
      ++state.keys;
      state.received.notify_all();
    }
  }
  std::vector<gneiss::tasks::task_completion> results;
  state.scheduler.poll(state.scope, results);
  for (const auto& result : results) {
    if (result.outcome.state != gneiss::tasks::task_state::succeeded) {
      state.failed = true;
    }
    state.task_ms.push_back(static_cast<double>(now_ns() - state.task_finished.load()) / 1e6);
    std::scoped_lock lock(state.mutex);
    ++state.tasks;
    state.received.notify_all();
  }
  if (state.failed || clock_type::now() > state.deadline) {
    state.failed = true;
    return gneiss_application_request_exit(app);
  }
  return GNEISS_SUCCESS;
} catch (...) {
  return GNEISS_ERROR_INTERNAL;
}
std::uint8_t close_requested(gneiss_application /*unused*/, void* opaque) {
  auto& state = *static_cast<response_context*>(opaque);
  state.close_ms = static_cast<double>(now_ns() - state.close_sent.load()) / 1e6;
  return 1U;
}
double percentile(std::vector<double> values, double fraction) {
  std::ranges::sort(values);
  return values.empty()
             ? 0.0
             : values[static_cast<std::size_t>(fraction * static_cast<double>(values.size() - 1U))];
}
} // namespace

int main(int argc, char** argv) try {
  response_context state;
  const bool inject_stall = argc > 2 && std::string_view(argv[2]) == "stall";
  state.stall = inject_stall;
  state.key_ms.reserve(64U);
  state.task_ms.reserve(64U);
  const auto title = "Gneiss Response " + std::to_string(GetCurrentProcessId());
  auto desc = gneiss_application_desc GNEISS_APPLICATION_DESC_INIT;
  desc.platform = GNEISS_APPLICATION_PLATFORM_GRANIT;
  desc.window_flags = GNEISS_APPLICATION_WINDOW_RESIZABLE_BIT;
  desc.window_title = title.data();
  desc.window_title_length = static_cast<std::uint32_t>(title.size());
  desc.window_width = 320U;
  desc.window_height = 240U;
  desc.user_data = &state;
  desc.update = update;
  desc.close_requested = close_requested;
  gneiss::application app;
  if (gneiss::application::create_native(desc, app).failed() ||
      gneiss::application_internal::attach_task_executor(app.get(), state.scheduler) !=
          GNEISS_SUCCESS) {
    return 1;
  }
  auto* const window = FindWindowA(nullptr, title.c_str());
  if (window == nullptr) {
    return 2;
  }
  // 单个未确认样本保证按键与任务时间戳不被下一个样本覆盖；发送线程不依赖主循环运行。
  std::jthread sender([&] {
    for (unsigned index = 1U; index <= 64U && !state.stopped; ++index) {
      std::this_thread::sleep_for(20ms);
      state.key_sent = now_ns();
      state.sent = index;
      gneiss::tasks::task_handle task;
      const auto submitted = state.scheduler.submit(
          {.name = "response", .scope = state.scope},
          [&](const gneiss::tasks::task_context&) {
            state.task_finished = now_ns();
            return gneiss::tasks::task_outcome{};
          },
          task);
      if (submitted != gneiss::tasks::submit_result::success ||
          !PostMessageW(window, WM_KEYDOWN, 'A', 0x001e0001) ||
          !PostMessageW(window, WM_KEYUP, 'A', static_cast<LPARAM>(0xc01e0001U))) {
        state.failed = true;
        break;
      }
      std::unique_lock lock(state.mutex);
      if (!state.received.wait_for(lock, 2s, [&] {
            return state.stopped || (state.keys >= index && state.tasks >= index);
          })) {
        state.failed = true;
        break;
      }
    }
    state.close_sent = now_ns();
    if (!PostMessageW(window, WM_CLOSE, 0U, 0)) {
      state.failed = true;
    }
  });
  const auto result = app.run();
  {
    std::scoped_lock lock(state.mutex);
    state.stopped = true;
    state.received.notify_all();
  }
  sender.join();
  const double key_max = percentile(state.key_ms, 1.0);
  const double task_max = percentile(state.task_ms, 1.0);
  const bool passed =
      result == gneiss::result::success && !state.failed && state.keys == 64U &&
      state.tasks == 64U && state.close_ms > 0.0 &&
      (inject_stall
           ? key_max >= 40.0
           : key_max <= 100.0 && task_max <= 100.0 && state.close_ms <= 100.0 &&
                 percentile(state.key_ms, 0.95) <= 33.3 && percentile(state.task_ms, 0.95) <= 33.3);
  if (argc > 1) {
    std::ofstream output(argv[1]);
    output << "{\"passed\":" << std::boolalpha << passed << ",\"keys\":" << state.keys.load()
           << ",\"tasks\":" << state.tasks.load()
           << ",\"key_p95_ms\":" << percentile(state.key_ms, 0.95) << ",\"key_max_ms\":" << key_max
           << ",\"task_p95_ms\":" << percentile(state.task_ms, 0.95)
           << ",\"task_max_ms\":" << task_max << ",\"close_ms\":" << state.close_ms
           << ",\"injected_stall\":" << inject_stall << "}\n";
    if (!output) {
      return 4;
    }
  }
  std::printf("输入=%u，任务=%u，输入最大=%.3f ms，任务最大=%.3f ms，关闭=%.3f ms\n",
              state.keys.load(), state.tasks.load(), key_max, task_max, state.close_ms);
  return passed ? 0 : 3;
} catch (...) {
  return 5;
}
