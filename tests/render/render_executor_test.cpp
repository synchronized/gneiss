// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/loop_progress.hpp"
#include "engine/core/result.hpp"
#include "engine/function/render/render_executor.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <vector>

namespace {
bool resumable_command_allows_frames(gneiss::engine::core::result terminal) {
  using namespace gneiss::engine::render;
  std::atomic_bool frame_executed{};
  auto started = std::make_shared<gneiss::engine::core::loop_progress>();
  auto lifetime = std::make_shared<int>(7);
  std::weak_ptr<int> retained = lifetime;
  threaded_render_executor executor;
  if (executor.initialize([&](auto&, auto&) {
        frame_executed.store(true);
        return gneiss::engine::core::result::success;
      }) != gneiss::engine::core::result::success) {
    return false;
  }
  const auto observed = started->snapshot();
  std::uint64_t command{};
  if (executor.submit_command(
          [&, lifetime](const auto& reporter) {
            reporter.report(render_command_stage::uploading, 0U, 1U);
            started->notify();
            return frame_executed.load() ? terminal : gneiss::engine::core::result::not_ready;
          },
          command, render_command_policy::resumable) != gneiss::engine::core::result::success) {
    return false;
  }
  lifetime.reset();
  const bool began = started->wait(observed, std::chrono::seconds(2));
  render_command_completion premature;
  const bool no_completion = !executor.try_take_command_completion(premature);
  const bool alive = !retained.expired();
  std::uint64_t frame{};
  const auto submitted = executor.submit_frame({}, frame, render_frame_policy::required);
  if (submitted != gneiss::engine::core::result::success) {
    frame_executed.store(true);
  }
  const auto flushed = executor.flush();
  render_command_completion completion;
  const bool completed = executor.try_take_command_completion(completion);
  render_frame_completion drawn;
  const bool rendered = executor.try_take_frame_completion(drawn);
  const auto stats = executor.query_stats();
  executor.stop();
  return began && no_completion && alive && submitted == gneiss::engine::core::result::success &&
         flushed == gneiss::engine::core::result::success && completed &&
         completion.sequence == command && completion.status == terminal && rendered &&
         drawn.sequence == frame && drawn.status == gneiss::engine::core::result::success &&
         stats.submitted_commands == 1U && stats.executed_commands == 1U && retained.expired() &&
         !executor.try_take_command_completion(completion);
}
} // namespace

int main() {
  if (!resumable_command_allows_frames(gneiss::engine::core::result::success) ||
      !resumable_command_allows_frames(gneiss::engine::core::result::io)) {
    return 24;
  }
  using namespace gneiss::engine::render;
  render_frame_packet inline_packet;
  inline_packet.window.width = 7U;
  inline_render_executor inline_executor(
      [](render_frame_packet& packet, render_execution_result& output) {
        output.needs_recreate = packet.window.width == 7U;
        if (packet.sequence == 0U) {
          return gneiss::engine::core::result::invalid_state;
        }
        return gneiss::engine::core::result::success;
      });
  render_execution_result inline_result;
  if (inline_executor.submit(std::move(inline_packet), inline_result) !=
          gneiss::engine::core::result::success ||
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
        output.presented = packet.window.width == 3U;
        output.gpu_timing_supported = true;
        if (packet.window.width == 3U) {
          output.gpu_timing_valid = true;
          output.gpu_timing_sample_sequence = 7U;
          output.gpu_frame_ms = 1.25F;
          output.gpu_shadow_ms = 0.25F;
          output.gpu_opaque_ms = 0.75F;
          output.gpu_tone_mapping_ms = 0.25F;
        }
        return gneiss::engine::core::result::success;
      },
      1U);
  if (initialize_result != gneiss::engine::core::result::success || !executor.is_running()) {
    return 2;
  }

  render_frame_packet first;
  first.window.width = 1U;
  std::uint64_t first_sequence = 0U;
  if (executor.submit_frame(std::move(first), first_sequence) !=
      gneiss::engine::core::result::success) {
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
  if (executor.submit_frame(std::move(second), second_sequence) !=
          gneiss::engine::core::result::success ||
      executor.submit_frame(std::move(third), third_sequence) !=
          gneiss::engine::core::result::success) {
    return 4;
  }
  {
    std::lock_guard lock(mutex);
    release_first = true;
    ready.notify_all();
  }
  if (executor.flush() != gneiss::engine::core::result::success) {
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
      stats.submitted_frames != 3U || stats.executed_frames != 2U || stats.presented_frames != 1U ||
      stats.pending_tasks != 0U || stats.pending_frames != 0U || stats.pending_commands != 0U ||
      stats.latest_frame_queue_wait_ms < 0.0F ||
      stats.maximum_frame_queue_wait_ms < stats.latest_frame_queue_wait_ms ||
      stats.latest_frame_capture_ms != 2.5F || stats.latest_copied_payload_bytes != 4096U ||
      !stats.gpu_timing_supported || !stats.latest_gpu_timing_valid ||
      stats.latest_gpu_timing_sample_sequence != 7U || stats.gpu_timing_sample_count != 1U ||
      stats.gpu_timing_unavailable_count != 1U || stats.latest_gpu_frame_ms != 1.25F ||
      stats.latest_gpu_shadow_ms != 0.25F || stats.latest_gpu_opaque_ms != 0.75F ||
      stats.latest_gpu_tone_mapping_ms != 0.25F) {
    return 6;
  }
  bool saw_dropped = false;
  bool saw_recreate = false;
  bool saw_reusable_packet = false;
  for (const auto& item : completions) {
    saw_dropped = saw_dropped || (item.sequence == second_sequence && item.dropped &&
                                  item.status == gneiss::engine::core::result::not_ready);
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
            return gneiss::engine::core::result::success;
          },
          command_sequence) != gneiss::engine::core::result::success) {
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
  if (executor.flush() != gneiss::engine::core::result::success) {
    return 10;
  }
  render_command_completion command_completion;
  if (!executor.try_take_command_completion(command_completion) ||
      command_completion.sequence != command_sequence ||
      command_completion.status != gneiss::engine::core::result::success ||
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
            return gneiss::engine::core::result::invalid_state;
          },
          failed_sequence) != gneiss::engine::core::result::success ||
      executor.flush() != gneiss::engine::core::result::success ||
      !executor.try_take_command_completion(command_completion) ||
      command_completion.sequence != failed_sequence ||
      command_completion.status != gneiss::engine::core::result::invalid_state ||
      command_completion.progress.completed_work != 1U ||
      command_completion.progress.total_work != 3U || !executor.is_running()) {
    return 12;
  }
  executor.record_skipped_frame_build();
  const auto final_stats = executor.query_stats();
  if (final_stats.submitted_commands != 2U || final_stats.executed_commands != 2U ||
      final_stats.rejected_commands != 0U || command_completion.progress.total_work != 3U) {
    return 13;
  }
  if (final_stats.skipped_frame_builds != 1U) {
    return 14;
  }
  executor.stop();
  executor.stop();
  if (executor.is_running()) {
    return 15;
  }

  bool required_blocked = false;
  bool release_required = false;
  threaded_render_executor required_executor;
  if (required_executor.initialize(
          [&](render_frame_packet& packet, render_execution_result&) {
            if (packet.window.width == 10U) {
              std::unique_lock lock(mutex);
              required_blocked = true;
              ready.notify_all();
              ready.wait(lock, [&] { return release_required; });
            }
            return gneiss::engine::core::result::success;
          },
          1U) != gneiss::engine::core::result::success) {
    return 16;
  }
  render_frame_packet blocking_packet;
  blocking_packet.window.width = 10U;
  std::uint64_t blocking_sequence{};
  if (required_executor.submit_frame(std::move(blocking_packet), blocking_sequence) !=
      gneiss::engine::core::result::success) {
    return 16;
  }
  {
    std::unique_lock lock(mutex);
    ready.wait(lock, [&] { return required_blocked; });
  }
  render_frame_packet required_packet;
  required_packet.window.width = 20U;
  std::uint64_t required_sequence{};
  if (required_executor.submit_frame(std::move(required_packet), required_sequence,
                                     render_frame_policy::required) !=
      gneiss::engine::core::result::success) {
    return 17;
  }
  render_frame_packet replaceable_packet;
  replaceable_packet.window.width = 30U;
  std::uint64_t rejected_sequence{};
  if (required_executor.submit_frame(std::move(replaceable_packet), rejected_sequence) !=
      gneiss::engine::core::result::not_ready) {
    return 18;
  }
  render_frame_packet second_required_packet;
  second_required_packet.window.width = 40U;
  if (required_executor.submit_frame(std::move(second_required_packet), rejected_sequence,
                                     render_frame_policy::required) !=
      gneiss::engine::core::result::not_ready) {
    return 19;
  }
  {
    std::lock_guard lock(mutex);
    release_required = true;
    ready.notify_all();
  }
  if (required_executor.flush() != gneiss::engine::core::result::success) {
    return 20;
  }
  const auto required_stats = required_executor.query_stats();
  if (required_stats.submitted_required_frames != 1U ||
      required_stats.executed_required_frames != 1U ||
      required_stats.rejected_required_frames != 1U || required_stats.replaced_frames != 0U ||
      required_sequence == 0U) {
    return 21;
  }
  auto signal = std::make_shared<gneiss::engine::core::loop_progress>();
  required_executor.set_completion_notification(signal);
  const auto observed = signal->snapshot();
  std::uint64_t notified_sequence{};
  if (required_executor.submit_command(
          [](const auto&) { return gneiss::engine::core::result::internal; }, notified_sequence) !=
          gneiss::engine::core::result::success ||
      !signal->wait(observed, std::chrono::seconds(2))) {
    return 22;
  }
  render_command_completion notified;
  if (!required_executor.try_take_command_completion(notified) ||
      notified.sequence != notified_sequence ||
      notified.status != gneiss::engine::core::result::internal) {
    return 23;
  }
  required_executor.stop();
  threaded_render_executor once_executor;
  unsigned calls{};
  std::uint64_t once_sequence{};
  if (once_executor.initialize([](auto&, auto&) {
        return gneiss::engine::core::result::success;
      }) != gneiss::engine::core::result::success ||
      once_executor.submit_command(
          [&](const auto&) {
            ++calls;
            return gneiss::engine::core::result::not_ready;
          },
          once_sequence) != gneiss::engine::core::result::success ||
      once_executor.flush() != gneiss::engine::core::result::success) {
    return 25;
  }
  render_command_completion once;
  if (!once_executor.try_take_command_completion(once) || calls != 1U ||
      once.status != gneiss::engine::core::result::not_ready || once.sequence != once_sequence) {
    return 26;
  }
  return 0;
}
