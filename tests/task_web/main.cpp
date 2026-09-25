// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../core/cooperative_scheduler_contract.h"

#include <cstdio>
#include <emscripten.h>

namespace {
using namespace gneiss::tasks;
struct browser_host {
  task_scheduler scheduler{{.mode = execution_mode::cooperative}};
  task_scope scope{scheduler.make_scope()};
  unsigned frames{};
  unsigned ran_at{};
};
browser_host host;
void tick() {
  try {
    ++host.frames;
    if (host.frames == 1U) {
      task_handle handle;
      if (host.scheduler.submit(
              {.scope = host.scope,
               .not_before = std::chrono::steady_clock::now() + std::chrono::milliseconds(40)},
              [](const auto&) {
                host.ran_at = host.frames;
                return task_outcome{};
              },
              handle) != submit_result::success) {
        throw std::runtime_error("浏览器任务提交失败");
      }
    }
    (void)host.scheduler.run_ready();
    if (host.ran_at != 0U) {
      if (host.ran_at <= 1U) {
        throw std::runtime_error("未跨事件循环执行");
      }
      host.scheduler.request_stop();
      if (!host.scheduler.stopped() || !host.scheduler.try_close_scope(host.scope)) {
        throw std::runtime_error("浏览器非阻塞关闭失败");
      }
      std::puts("GNEISS_TASK_WEB_PASS");
      EM_ASM({ document.body.dataset.testResult = 'passed'; });
      emscripten_cancel_main_loop();
    }
  } catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    EM_ASM({ document.body.dataset.testResult = 'failed'; });
    emscripten_cancel_main_loop();
  }
}
}
int main() {
  try {
    cooperative_scheduler_contract();
    bool rejected{};
    try {
      task_scheduler unavailable;
    } catch (const std::invalid_argument&) {
      rejected = true;
    }
    if (!rejected) {
      throw std::runtime_error("无线程产物接受了线程池模式");
    }
  } catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    EM_ASM({ document.body.dataset.testResult = 'failed'; });
    return 1;
  }
  emscripten_set_main_loop(tick, 60, true);
}
