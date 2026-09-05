// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "render/render_executor.h"

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <vector>

int main() {
  using namespace gneiss::render_internal;
  render_frame_packet inline_packet;
  inline_packet.window.width = 7U;
  inline_render_executor inline_executor(
      [](render_frame_packet& packet, render_execution_result& output) {
        output.needs_recreate = packet.window.width == 7U;
        return GNEISS_SUCCESS;
      });
  render_execution_result inline_result;
  if (inline_executor.submit(std::move(inline_packet), inline_result) != GNEISS_SUCCESS ||
      !inline_result.needs_recreate) {
    return 1;
  }

  std::mutex mutex;
  std::condition_variable ready;
  bool first_started = false;
  bool release_first = false;
  std::vector<std::uint32_t> executed;
  threaded_render_executor executor;
  const auto initialize_result = executor.initialize(
      [&](render_frame_packet& packet, render_execution_result& output) {
        {
          std::unique_lock lock(mutex);
          executed.push_back(packet.window.width);
          if (packet.window.width == 1U) {
            first_started = true;
            ready.notify_all();
            ready.wait(lock, [&] { return release_first; });
          }
        }
        output.needs_recreate = packet.window.needs_recreate;
        return GNEISS_SUCCESS;
      },
      1U);
  if (initialize_result != GNEISS_SUCCESS || !executor.is_running()) {
    return 2;
  }

  render_frame_packet first;
  first.window.width = 1U;
  std::uint64_t first_sequence = 0U;
  if (executor.submit_frame(std::move(first), first_sequence) != GNEISS_SUCCESS) {
    return 3;
  }
  {
    std::unique_lock lock(mutex);
    ready.wait(lock, [&] { return first_started; });
  }
  render_frame_packet second;
  second.window.width = 2U;
  std::uint64_t second_sequence = 0U;
  render_frame_packet third;
  third.window.width = 3U;
  third.window.needs_recreate = true;
  third.capture.capture_ms = 2.5F;
  third.capture.copied_payload_bytes = 4096U;
  std::uint64_t third_sequence = 0U;
  if (executor.submit_frame(std::move(second), second_sequence) != GNEISS_SUCCESS ||
      executor.submit_frame(std::move(third), third_sequence) != GNEISS_SUCCESS) {
    return 4;
  }
  {
    std::lock_guard lock(mutex);
    release_first = true;
    ready.notify_all();
  }
  if (executor.flush() != GNEISS_SUCCESS) {
    return 5;
  }

  std::vector<render_frame_completion> completions;
  render_frame_completion completion;
  while (executor.try_take_frame_completion(completion)) {
    completions.push_back(completion);
  }
  const auto stats = executor.query_stats();
  if (executed != std::vector<std::uint32_t>{1U, 3U} || completions.size() != 3U ||
      stats.replaced_frames != 1U || stats.pending_high_watermark == 0U ||
      stats.submitted_frames != 3U || stats.executed_frames != 2U || stats.pending_tasks != 0U ||
      stats.pending_frames != 0U || stats.pending_commands != 0U ||
      stats.latest_frame_queue_wait_ms < 0.0F ||
      stats.maximum_frame_queue_wait_ms < stats.latest_frame_queue_wait_ms ||
      stats.latest_frame_capture_ms != 2.5F || stats.latest_copied_payload_bytes != 4096U) {
    return 6;
  }
  bool saw_dropped = false;
  bool saw_recreate = false;
  bool saw_reusable_packet = false;
  for (const auto& item : completions) {
    saw_dropped = saw_dropped || (item.sequence == second_sequence && item.dropped &&
                                  item.status == GNEISS_ERROR_NOT_READY);
    saw_recreate =
        saw_recreate || (item.sequence == third_sequence && item.execution.needs_recreate);
    saw_reusable_packet =
        saw_reusable_packet ||
        (item.sequence == third_sequence && item.reusable_packet.window.width == 3U &&
         item.reusable_packet.capture.copied_payload_bytes == 4096U);
  }
  if (!saw_dropped || !saw_recreate || first_sequence == second_sequence ||
      second_sequence == third_sequence || !saw_reusable_packet) {
    return 7;
  }

  bool command_started = false;
  bool release_command = false;
  std::uint64_t command_sequence = 0U;
  if (executor.submit_command(
          [&](const render_command_reporter& reporter) {
            reporter.report(render_command_stage::uploading, 4U, 4U);
            std::unique_lock lock(mutex);
            command_started = true;
            ready.notify_all();
            ready.wait(lock, [&] { return release_command; });
            return GNEISS_SUCCESS;
          },
          command_sequence) != GNEISS_SUCCESS) {
    return 8;
  }
  {
    std::unique_lock lock(mutex);
    ready.wait(lock, [&] { return command_started; });
  }
  render_command_status command_status;
  if (!executor.query_command_status(command_status) || !command_status.active ||
      command_status.sequence != command_sequence ||
      command_status.progress.stage != render_command_stage::uploading ||
      command_status.progress.completed_work != 4U) {
    return 9;
  }
  {
    std::lock_guard lock(mutex);
    release_command = true;
    ready.notify_all();
  }
  if (executor.flush() != GNEISS_SUCCESS) {
    return 10;
  }
  render_command_completion command_completion;
  if (!executor.try_take_command_completion(command_completion) ||
      command_completion.sequence != command_sequence ||
      command_completion.status != GNEISS_SUCCESS ||
      command_completion.progress.stage != render_command_stage::completed ||
      command_completion.progress.completed_work != 4U ||
      !executor.query_command_status(command_status) || command_status.active ||
      command_status.sequence != command_sequence ||
      command_status.progress.stage != render_command_stage::completed) {
    return 11;
  }
  std::uint64_t failed_sequence = 0U;
  if (executor.submit_command(
          [](const render_command_reporter& reporter) {
            reporter.report(render_command_stage::uploading, 1U, 3U);
            return GNEISS_ERROR_INVALID_STATE;
          },
          failed_sequence) != GNEISS_SUCCESS ||
      executor.flush() != GNEISS_SUCCESS ||
      !executor.try_take_command_completion(command_completion) ||
      command_completion.sequence != failed_sequence ||
      command_completion.status != GNEISS_ERROR_INVALID_STATE ||
      command_completion.progress.completed_work != 1U ||
      command_completion.progress.total_work != 3U || !executor.is_running()) {
    return 12;
  }
  const auto final_stats = executor.query_stats();
  if (final_stats.submitted_commands != 2U || final_stats.executed_commands != 2U ||
      final_stats.rejected_commands != 0U || command_completion.progress.total_work != 3U) {
    return 13;
  }
  executor.stop();
  executor.stop();
  return executor.is_running() ? 14 : 0;
}
