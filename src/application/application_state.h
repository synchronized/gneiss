// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPLICATION_APPLICATION_STATE_H_
#define GNEISS_APPLICATION_APPLICATION_STATE_H_

#include <gneiss/application.h>
#include <gneiss/input.h>

#include "asset/resource_cache.h"
#include "asset/virtual_file_system.h"
#include "render/render_asset_loader.h"
#include "render/render_resource_service.h"
#include "scene/scene_instance_service.h"

#include <cstdint>
#include <memory>
#include <thread>

namespace gneiss::application_internal {

#ifdef GNEISS_HAS_GRANIT_PLATFORM
class granit_platform;
class granit_render_service;
#endif

class application_state final {
public:
  explicit application_state(const gneiss_application_desc& desc) noexcept;
  ~application_state() noexcept;

  application_state(const application_state&) = delete;
  application_state& operator=(const application_state&) = delete;
  application_state(application_state&&) = delete;
  application_state& operator=(application_state&&) = delete;

  [[nodiscard]] gneiss_result initialize() noexcept;
  [[nodiscard]] gneiss_result run(gneiss_application handle,
                                  std::uint64_t max_frame_count) noexcept;
  [[nodiscard]] bool is_owner_thread() const noexcept;
  [[nodiscard]] gneiss_world world() const noexcept { return world_; }
  [[nodiscard]] render_internal::render_resource_service& resources() noexcept {
    return resources_;
  }
  [[nodiscard]] render_internal::render_asset_loader& asset_loader() noexcept {
    return asset_loader_;
  }
  [[nodiscard]] scene_internal::scene_instance_service* scenes() noexcept { return scenes_.get(); }
  [[nodiscard]] const gneiss_keyboard_state& keyboard_state() const noexcept {
    return keyboard_state_;
  }
  [[nodiscard]] const gneiss_pointer_state& pointer_state() const noexcept {
    return pointer_state_;
  }
  void request_exit() noexcept { should_exit_ = true; }
  void set_paused(bool value) noexcept { is_paused_ = value; }

private:
  [[nodiscard]] std::uint64_t now_ns() const noexcept;
  [[nodiscard]] gneiss_result poll_events(bool& out_should_close) noexcept;
#ifdef GNEISS_HAS_GRANIT_PLATFORM
  [[nodiscard]] gneiss_result render_frame() noexcept;
#endif
  void shutdown() noexcept;

  gneiss_application_desc desc_;
  render_internal::render_resource_service resources_;
  asset_internal::virtual_file_system asset_file_system_;
  asset_internal::resource_cache asset_cache_;
  render_internal::render_asset_loader asset_loader_;
  gneiss_world world_ = GNEISS_NULL_WORLD;
  std::unique_ptr<scene_internal::scene_instance_service> scenes_;
  std::thread::id owner_thread_;
  std::uint64_t frame_index_ = 0;
  std::uint64_t elapsed_ns_ = 0;
  std::uint64_t previous_time_ns_ = 0;
  bool platform_initialized_ = false;
  bool is_running_ = false;
  bool is_paused_ = false;
  bool should_exit_ = false;
  gneiss_keyboard_state keyboard_state_ = GNEISS_KEYBOARD_STATE_INIT;
  gneiss_pointer_state pointer_state_ = GNEISS_POINTER_STATE_INIT;
#ifdef GNEISS_HAS_GRANIT_PLATFORM
  std::unique_ptr<granit_platform> granit_platform_;
  std::unique_ptr<granit_render_service> granit_render_service_;
#endif
};

} // namespace gneiss::application_internal

#endif
