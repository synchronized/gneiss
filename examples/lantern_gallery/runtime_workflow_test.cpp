// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_process.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <optional>
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

std::optional<std::array<float, 4>> root_rotation(const gneiss::editor::runtime_process& process) {
  const auto& nodes = process.scene_mirror().nodes();
  const auto root =
      std::ranges::find_if(nodes, [](const auto& node) { return !node.parent.is_valid(); });
  if (root == nodes.end()) {
    return std::nullopt;
  }
  return std::to_array(root->local_transform.rotation);
}

bool rotation_changed(const std::array<float, 4>& left,
                      const std::array<float, 4>& right) noexcept {
  constexpr float tolerance = 0.0001F;
  for (std::size_t index = 0U; index < left.size(); ++index) {
    if (std::abs(left[index] - right[index]) > tolerance) {
      return true;
    }
  }
  return false;
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
  if (!pump_until(process, 3s, [&] {
        return progress_count(process, first_session) >= 1U && root_rotation(process).has_value() &&
               process.statistics().fixed_update_count != 0U;
      })) {
    return 2;
  }
  const auto initial_rotation = *root_rotation(process);
  if (!pump_until(process, 3s,
                  [&] {
                    const auto current = root_rotation(process);
                    return current.has_value() && rotation_changed(initial_rotation, *current);
                  }) ||
      process.request_pause() != gneiss::result::success || !pump_until(process, 3s, [&] {
        return process.control_state() == gneiss::editor::runtime_control_state::paused;
      })) {
    return 2;
  }

  // 暂停确认后先排空已在传输途中的事件，再观察游戏更新是否保持静止。
  // 检查消息使用独立的有界队列；在全量测试负载下，暂停确认可能早于此前快照完成应用。
  const auto drain_deadline = std::chrono::steady_clock::now() + 500ms;
  while (std::chrono::steady_clock::now() < drain_deadline) {
    process.update();
    std::this_thread::sleep_for(10ms);
  }
  const auto paused_count = progress_count(process, first_session);
  const auto paused_rotation = root_rotation(process);
  const auto observation_deadline = std::chrono::steady_clock::now() + 700ms;
  while (std::chrono::steady_clock::now() < observation_deadline) {
    process.update();
    std::this_thread::sleep_for(10ms);
  }
  const auto after_pause_rotation = root_rotation(process);
  if (!paused_rotation.has_value() || !after_pause_rotation.has_value() ||
      rotation_changed(*paused_rotation, *after_pause_rotation) ||
      progress_count(process, first_session) != paused_count ||
      process.request_resume() != gneiss::result::success ||
      !pump_until(process, 3s,
                  [&] {
                    return process.control_state() ==
                               gneiss::editor::runtime_control_state::running &&
                           progress_count(process, first_session) > paused_count;
                  }) ||
      !pump_until(process, 3s,
                  [&] {
                    const auto current = root_rotation(process);
                    return current.has_value() && rotation_changed(*paused_rotation, *current);
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
