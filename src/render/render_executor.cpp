// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "render/render_executor.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <new>
#include <ranges>
#include <thread>
#include <utility>

namespace gneiss::render_internal {

void render_command_reporter::report(render_command_stage stage, std::uint64_t completed_work,
                                     std::uint64_t total_work) const {
  if (publish_) {
    publish_(stage, completed_work, total_work);
  }
}

inline_render_executor::inline_render_executor(render_frame_callback callback)
    : callback_(std::move(callback)) {}

gneiss_result inline_render_executor::submit(render_frame_packet packet,
                                             render_execution_result& output) {
  output = {};
  return callback_ ? callback_(packet, output) : GNEISS_ERROR_INVALID_ARGUMENT;
}

struct threaded_render_executor::state final {
  enum class task_kind { frame, command };

  struct queued_task final {
    task_kind kind{task_kind::frame};
    std::uint64_t sequence{};
    render_frame_packet packet;
    render_command_callback command;
    std::chrono::steady_clock::time_point enqueued_at;
  };

  std::mutex mutex;
  std::condition_variable work_ready;
  std::condition_variable idle;
  std::deque<queued_task> pending;
  std::deque<render_frame_completion> completed_frames;
  std::deque<render_command_completion> completed_commands;
  std::thread worker;
  render_frame_callback frame_callback;
  std::size_t maximum_pending_frames{3U};
  std::uint64_t next_sequence{1U};
  bool executing{};
  bool stopping{};
  render_queue_stats stats;
  render_command_status command_status;
  bool has_command_status{};

  void run() noexcept;
};

void threaded_render_executor::state::run() noexcept {
  for (;;) {
    queued_task task;
    {
      std::unique_lock lock(mutex);
      work_ready.wait(lock, [&] { return stopping || !pending.empty(); });
      if (stopping && pending.empty()) {
        break;
      }
      task = std::move(pending.front());
      pending.pop_front();
      executing = true;
    }

    render_frame_completion frame_completion;
    render_command_completion command_completion;
    if (task.kind == task_kind::frame) {
      frame_completion.sequence = task.sequence;
      frame_completion.execution.queue_wait_ms =
          std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() -
                                                   task.enqueued_at)
              .count();
      try {
        frame_completion.status = frame_callback(task.packet, frame_completion.execution);
      } catch (...) {
        frame_completion.status = GNEISS_ERROR_INTERNAL;
      }
    } else {
      command_completion.sequence = task.sequence;
      command_completion.progress.stage = render_command_stage::preparing;
      const render_command_reporter reporter{
          [this, sequence = task.sequence](render_command_stage stage, std::uint64_t completed_work,
                                           std::uint64_t total_work) {
            std::lock_guard lock(mutex);
            command_status = {.sequence = sequence,
                              .progress = {.stage = stage,
                                           .completed_work = completed_work,
                                           .total_work = total_work},
                              .active = true};
            has_command_status = true;
          }};
      reporter.report(render_command_stage::preparing, 0U, 0U);
      try {
        command_completion.status = task.command(reporter);
      } catch (...) {
        command_completion.status = GNEISS_ERROR_INTERNAL;
      }
      {
        std::lock_guard lock(mutex);
        command_completion.progress = command_status.progress;
        command_completion.progress.stage = render_command_stage::completed;
        command_status = {
            .sequence = task.sequence, .progress = command_completion.progress, .active = false};
      }
    }
    {
      std::lock_guard lock(mutex);
      if (task.kind == task_kind::frame) {
        completed_frames.push_back(std::move(frame_completion));
      } else {
        completed_commands.push_back(std::move(command_completion));
      }
      executing = false;
      if (pending.empty()) {
        idle.notify_all();
      }
    }
  }
}

threaded_render_executor::threaded_render_executor() = default;

threaded_render_executor::~threaded_render_executor() { stop(); }

gneiss_result threaded_render_executor::initialize(render_frame_callback callback,
                                                   std::size_t maximum_pending_frames) noexcept {
  if (!callback || maximum_pending_frames == 0U || state_) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto candidate = std::make_unique<state>();
    candidate->frame_callback = std::move(callback);
    candidate->maximum_pending_frames = maximum_pending_frames;
    candidate->worker = std::thread(&state::run, candidate.get());
    state_ = std::move(candidate);
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INITIALIZATION_FAILED;
  }
}

gneiss_result threaded_render_executor::submit_frame(render_frame_packet packet,
                                                     std::uint64_t& out_sequence) noexcept {
  if (!state_) {
    return GNEISS_ERROR_NOT_READY;
  }
  try {
    std::lock_guard lock(state_->mutex);
    if (state_->stopping) {
      return GNEISS_ERROR_NOT_READY;
    }
    out_sequence = state_->next_sequence++;
    const auto frame_count = static_cast<std::size_t>(std::ranges::count_if(
        state_->pending, [](const auto& task) { return task.kind == state::task_kind::frame; }));
    if (frame_count >= state_->maximum_pending_frames) {
      const auto replace = std::ranges::find_if(
          state_->pending, [](const auto& task) { return task.kind == state::task_kind::frame; });
      const auto dropped_sequence = replace->sequence;
      state_->pending.erase(replace);
      state_->completed_frames.push_back({.sequence = dropped_sequence,
                                          .status = GNEISS_ERROR_NOT_READY,
                                          .execution = {},
                                          .dropped = true});
      ++state_->stats.replaced_frames;
    }
    state_->pending.push_back({.kind = state::task_kind::frame,
                               .sequence = out_sequence,
                               .packet = std::move(packet),
                               .command = {},
                               .enqueued_at = std::chrono::steady_clock::now()});
    state_->stats.pending_high_watermark =
        std::max(state_->stats.pending_high_watermark, state_->pending.size());
    state_->work_ready.notify_one();
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result threaded_render_executor::submit_command(render_command_callback command,
                                                       std::uint64_t& out_sequence) noexcept {
  if (!state_) {
    return GNEISS_ERROR_NOT_READY;
  }
  if (!command) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    std::lock_guard lock(state_->mutex);
    if (state_->stopping ||
        state_->pending.size() >= state_->maximum_pending_frames + std::size_t{8U}) {
      return GNEISS_ERROR_NOT_READY;
    }
    out_sequence = state_->next_sequence++;
    state_->pending.push_back({.kind = state::task_kind::command,
                               .sequence = out_sequence,
                               .packet = {},
                               .command = std::move(command),
                               .enqueued_at = std::chrono::steady_clock::now()});
    state_->stats.pending_high_watermark =
        std::max(state_->stats.pending_high_watermark, state_->pending.size());
    state_->work_ready.notify_one();
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

bool threaded_render_executor::try_take_frame_completion(render_frame_completion& output) noexcept {
  if (!state_) {
    return false;
  }
  std::lock_guard lock(state_->mutex);
  if (state_->completed_frames.empty()) {
    return false;
  }
  output = std::move(state_->completed_frames.front());
  state_->completed_frames.pop_front();
  return true;
}

bool threaded_render_executor::try_take_command_completion(
    render_command_completion& output) noexcept {
  if (!state_) {
    return false;
  }
  std::lock_guard lock(state_->mutex);
  if (state_->completed_commands.empty()) {
    return false;
  }
  output = std::move(state_->completed_commands.front());
  state_->completed_commands.pop_front();
  return true;
}

bool threaded_render_executor::query_command_status(render_command_status& output) const noexcept {
  if (!state_) {
    return false;
  }
  std::lock_guard lock(state_->mutex);
  if (!state_->has_command_status) {
    return false;
  }
  output = state_->command_status;
  return true;
}

render_queue_stats threaded_render_executor::query_stats() const noexcept {
  if (!state_) {
    return {};
  }
  std::lock_guard lock(state_->mutex);
  return state_->stats;
}

gneiss_result threaded_render_executor::flush() noexcept {
  if (!state_) {
    return GNEISS_ERROR_NOT_READY;
  }
  std::unique_lock lock(state_->mutex);
  state_->idle.wait(lock, [&] { return state_->pending.empty() && !state_->executing; });
  return GNEISS_SUCCESS;
}

void threaded_render_executor::stop() noexcept {
  if (!state_) {
    return;
  }
  static_cast<void>(flush());
  {
    std::lock_guard lock(state_->mutex);
    state_->stopping = true;
    state_->work_ready.notify_all();
  }
  if (state_->worker.joinable()) {
    state_->worker.join();
  }
  state_.reset();
}

bool threaded_render_executor::is_running() const noexcept {
  if (!state_) {
    return false;
  }
  std::lock_guard lock(state_->mutex);
  return !state_->stopping;
}

} // namespace gneiss::render_internal
