// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "application/application_state.h"

#include <chrono>

namespace gneiss::application_internal {

application_state::application_state(const gneiss_application_desc& desc) noexcept
    : desc_(desc), owner_thread_(std::this_thread::get_id()) {}

application_state::~application_state() noexcept { shutdown(); }

gneiss_result application_state::initialize() noexcept {
  if (desc_.initialize != nullptr) {
    const auto result = desc_.initialize(desc_.user_data);
    if (result != GNEISS_SUCCESS) {
      // 初始化回调可能已获得部分资源，因此失败时也执行配对清理。
      platform_initialized_ = true;
      shutdown();
      return result;
    }
  }
  platform_initialized_ = true;

  const gneiss_world_desc world_desc = GNEISS_WORLD_DESC_INIT;
  const auto result = gneiss_world_create(&world_desc, &world_);
  if (result != GNEISS_SUCCESS) {
    shutdown();
    return result;
  }
  return GNEISS_SUCCESS;
}

std::uint64_t application_state::now_ns() const noexcept {
  if (desc_.now_ns != nullptr) {
    return desc_.now_ns(desc_.user_data);
  }
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): 句柄与帧数虽同宽但语义明确。
gneiss_result application_state::run(gneiss_application handle,
                                     std::uint64_t max_frame_count) noexcept {
  if (is_running_) {
    return GNEISS_ERROR_INVALID_STATE;
  }
  is_running_ = true;
  should_exit_ = false;
  previous_time_ns_ = now_ns();
  std::uint64_t frames_run = 0;

  while (!should_exit_ && (max_frame_count == 0U || frames_run < max_frame_count)) {
    if (desc_.poll_events != nullptr) {
      uint8_t should_close = 0;
      const auto poll_result = desc_.poll_events(desc_.user_data, &should_close);
      if (poll_result != GNEISS_SUCCESS) {
        is_running_ = false;
        return poll_result;
      }
      if (should_close != 0U) {
        break;
      }
    }

    const auto current_time_ns = now_ns();
    const auto raw_delta =
        current_time_ns >= previous_time_ns_ ? current_time_ns - previous_time_ns_ : 0U;
    previous_time_ns_ = current_time_ns;
    const auto delta_ns = is_paused_ ? 0U : raw_delta;
    elapsed_ns_ += delta_ns;
    const gneiss_frame_time time = {
        .frame_index = frame_index_,
        .delta_ns = delta_ns,
        .elapsed_ns = elapsed_ns_,
        .is_paused = static_cast<std::uint8_t>(is_paused_ ? 1U : 0U),
        .reserved = {},
    };
    if (desc_.update != nullptr) {
      const auto update_result = desc_.update(handle, &time, desc_.user_data);
      if (update_result != GNEISS_SUCCESS) {
        is_running_ = false;
        return update_result;
      }
    }
    ++frame_index_;
    ++frames_run;
  }

  is_running_ = false;
  return GNEISS_SUCCESS;
}

bool application_state::is_owner_thread() const noexcept {
  return owner_thread_ == std::this_thread::get_id();
}

void application_state::shutdown() noexcept {
  if (world_ != GNEISS_NULL_WORLD) {
    (void)gneiss_world_destroy(world_);
    world_ = GNEISS_NULL_WORLD;
  }
  if (platform_initialized_) {
    if (desc_.shutdown != nullptr) {
      desc_.shutdown(desc_.user_data);
    }
    platform_initialized_ = false;
  }
}

} // namespace gneiss::application_internal
