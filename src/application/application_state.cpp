// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "application/application_state.h"

#include "asset/native_file_system.h"

#ifdef GNEISS_HAS_GRANIT_PLATFORM
#include "platform/granit/granit_platform.h"
#include "render/granit/granit_render_service.h"
#endif

#include <chrono>
#include <new>

namespace gneiss::application_internal {

application_state::application_state(const gneiss_application_desc& desc) noexcept
    : desc_(desc), asset_loader_(asset_file_system_, asset_cache_, resources_),
      owner_thread_(std::this_thread::get_id()) {}

application_state::~application_state() noexcept { shutdown(); }

gneiss_result application_state::initialize() noexcept {
  if (!resources_.is_valid()) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  }
  if (desc_.asset_root != nullptr || desc_.asset_root_length != 0U) {
    if (desc_.asset_root == nullptr || desc_.asset_root_length == 0U) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    std::shared_ptr<asset_internal::native_file_system> native_file_system;
    try {
      native_file_system = std::make_shared<asset_internal::native_file_system>();
    } catch (const std::bad_alloc&) {
      return GNEISS_ERROR_OUT_OF_MEMORY;
    } catch (...) {
      return GNEISS_ERROR_INTERNAL;
    }
    auto mount_result =
        native_file_system->initialize(std::string_view(desc_.asset_root, desc_.asset_root_length));
    if (mount_result == GNEISS_SUCCESS) {
      mount_result = asset_file_system_.mount("asset://", std::move(native_file_system));
    }
    if (mount_result != GNEISS_SUCCESS) {
      return mount_result;
    }
  }
  if (desc_.platform == GNEISS_APPLICATION_PLATFORM_GRANIT) {
#ifdef GNEISS_HAS_GRANIT_PLATFORM
    try {
      granit_platform_ = std::make_unique<granit_platform>();
    } catch (const std::bad_alloc&) {
      return GNEISS_ERROR_OUT_OF_MEMORY;
    } catch (...) {
      return GNEISS_ERROR_INTERNAL;
    }
    const auto platform_result = granit_platform_->initialize(desc_);
    if (platform_result != GNEISS_SUCCESS) {
      granit_platform_.reset();
      return platform_result;
    }
    try {
      granit_render_service_ = std::make_unique<granit_render_service>();
    } catch (const std::bad_alloc&) {
      granit_platform_.reset();
      return GNEISS_ERROR_OUT_OF_MEMORY;
    } catch (...) {
      granit_platform_.reset();
      return GNEISS_ERROR_INTERNAL;
    }
    const auto render_result =
        granit_render_service_->initialize(granit_platform_->native_window());
    if (render_result != GNEISS_SUCCESS) {
      granit_render_service_.reset();
      granit_platform_.reset();
      return render_result;
    }
#else
    return GNEISS_ERROR_UNSUPPORTED;
#endif
  }
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

gneiss_result application_state::poll_events(bool& out_should_close) noexcept {
  out_should_close = false;
#ifdef GNEISS_HAS_GRANIT_PLATFORM
  if (granit_platform_ != nullptr) {
    return granit_platform_->poll(out_should_close);
  }
#endif
  if (desc_.poll_events == nullptr) {
    return GNEISS_SUCCESS;
  }
  uint8_t should_close = 0;
  const auto result = desc_.poll_events(desc_.user_data, &should_close);
  out_should_close = should_close != 0U;
  return result;
}

#ifdef GNEISS_HAS_GRANIT_PLATFORM
gneiss_result application_state::render_frame() noexcept {
  if (granit_render_service_ == nullptr) {
    return GNEISS_SUCCESS;
  }
  world_internal::render_snapshot snapshot;
  const auto snapshot_result = world_internal::get_render_snapshot(world_, snapshot);
  return snapshot_result == GNEISS_SUCCESS
             ? granit_render_service_->render(granit_platform_->native_window(), snapshot,
                                              resources_)
             : snapshot_result;
}
#endif

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
    bool should_close = false;
    const auto poll_result = poll_events(should_close);
    if (poll_result != GNEISS_SUCCESS) {
      is_running_ = false;
      return poll_result;
    }
    if (should_close) {
      break;
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
#ifdef GNEISS_HAS_GRANIT_PLATFORM
    const auto render_result = render_frame();
    if (render_result != GNEISS_SUCCESS) {
      is_running_ = false;
      return render_result;
    }
#endif
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
#ifdef GNEISS_HAS_GRANIT_PLATFORM
  granit_render_service_.reset();
  granit_platform_.reset();
#endif
}

} // namespace gneiss::application_internal
