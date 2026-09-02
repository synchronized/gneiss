// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_process.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <thread>

namespace {

using namespace std::chrono_literals;

std::size_t progress_count(const gneiss::editor::runtime_process& process,
                           std::uint64_t session_id) {
  return static_cast<std::size_t>(
      std::ranges::count_if(process.console().entries(), [session_id](const auto& entry) {
        return entry.session_id == session_id &&
               entry.kind == gneiss::editor::console_entry_kind::structured &&
               entry.event.category == "runtime_progress" &&
               entry.event.message.starts_with("Lantern Gallery 运行帧=");
      }));
}

template <typename Predicate>
bool pump_until(gneiss::editor::runtime_process& process, std::chrono::milliseconds timeout,
                Predicate&& predicate) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    process.update();
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(10ms);
  }
  process.update();
  return predicate();
}

bool stop_session(gneiss::editor::runtime_process& process) {
  if (process.request_stop() != gneiss::result::success) {
    return false;
  }
  return pump_until(process, 5s, [&] { return !process.is_running(); }) &&
         process.exit_code() == 0 && process.received_shutdown_complete();
}

} // namespace

int main() try {
  gneiss::editor::runtime_process process;
  const gneiss::editor::runtime_launch_request request{
      std::filesystem::path{GNEISS_LANTERN_PROJECT}};
  const std::filesystem::path runtime{GNEISS_LANTERN_RUNTIME};

  if (process.start(runtime, request) != gneiss::result::success || !pump_until(process, 5s, [&] {
        return process.control_state() == gneiss::editor::runtime_control_state::running;
      })) {
    return 1;
  }
  const auto first_session = process.console().current_session_id();
  if (!pump_until(process, 3s, [&] { return progress_count(process, first_session) >= 2U; }) ||
      process.request_pause() != gneiss::result::success || !pump_until(process, 3s, [&] {
        return process.control_state() == gneiss::editor::runtime_control_state::paused;
      })) {
    return 2;
  }

  // 暂停确认后先排空已在传输途中的事件，再观察游戏更新是否保持静止。
  const auto drain_deadline = std::chrono::steady_clock::now() + 100ms;
  while (std::chrono::steady_clock::now() < drain_deadline) {
    process.update();
    std::this_thread::sleep_for(10ms);
  }
  const auto paused_count = progress_count(process, first_session);
  const auto observation_deadline = std::chrono::steady_clock::now() + 700ms;
  while (std::chrono::steady_clock::now() < observation_deadline) {
    process.update();
    std::this_thread::sleep_for(10ms);
  }
  if (progress_count(process, first_session) != paused_count ||
      process.request_resume() != gneiss::result::success ||
      !pump_until(process, 3s,
                  [&] {
                    return process.control_state() ==
                               gneiss::editor::runtime_control_state::running &&
                           progress_count(process, first_session) > paused_count;
                  }) ||
      !stop_session(process)) {
    return 3;
  }

  if (process.start(runtime, request) != gneiss::result::success || !pump_until(process, 5s, [&] {
        return process.control_state() == gneiss::editor::runtime_control_state::running;
      })) {
    return 4;
  }
  const auto second_session = process.console().current_session_id();
  if (second_session == first_session ||
      !pump_until(process, 3s, [&] { return progress_count(process, second_session) >= 1U; }) ||
      !stop_session(process)) {
    return 5;
  }
  return 0;
} catch (...) {
  return 99;
}
