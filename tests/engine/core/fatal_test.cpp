// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include <chrono>
#include <gneiss/engine/core/base/error.hpp>
#include <gneiss/engine/core/tasks/scheduler.hpp>
#include <new>
#include <stdexcept>
#include <string_view>
#include <thread>

using namespace gneiss;
void throw_unexpected() { throw std::runtime_error("fixture"); }
int main(int argc, char** argv) {
  if (argc != 2)
    return 2;
  const std::string_view mode{argv[1]};
  if (mode == "direct")
    fatal(fatal_reason::out_of_memory);
  if (mode == "terminate") {
    install_fatal_handler();
    []() noexcept { throw_unexpected(); }();
  }
  if (mode == "host" || mode == "oom") {
    try {
      if (mode == "oom")
        throw std::bad_alloc{};
      throw std::runtime_error("fixture");
    } catch (...) {
      fatal_current_exception();
    }
  }
  const bool worker = mode == "worker" || mode == "worker-oom";
  gneiss::tasks::scheduler scheduler({.workers = 1,
                                      .mode = worker ? gneiss::tasks::execution_mode::thread_pool
                                                     : gneiss::tasks::execution_mode::cooperative});
  const auto scope = scheduler.make_scope();
  gneiss::tasks::handle task;
  if (scheduler.submit(
          {.scope = scope},
          [mode](const auto&) -> gneiss::tasks::outcome {
            if (mode == "worker-oom")
              throw std::bad_alloc{};
            throw std::runtime_error("fixture");
          },
          task) != gneiss::tasks::submit_result::success)
    return 3;
  if (!worker)
    static_cast<void>(scheduler.run_ready());
  // 子进程的致命执行不能返回；超时由父测试兜底。
  std::this_thread::sleep_for(std::chrono::seconds(5));
  return 0;
}
