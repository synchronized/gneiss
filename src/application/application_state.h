// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPLICATION_APPLICATION_STATE_H_
#define GNEISS_APPLICATION_APPLICATION_STATE_H_

#include <gneiss/application.h>

#include "render/render_resource_service.h"

#include <cstdint>
#ifdef GNEISS_HAS_GRANIT_PLATFORM
#include <memory>
#endif
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
  void request_exit() noexcept { should_exit_ = true; }
  void set_paused(bool value) noexcept { is_paused_ = value; }

private:
  [[nodiscard]] std::uint64_t now_ns() const noexcept;
  [[nodiscard]] gneiss_result poll_events(bool& out_should_close) noexcept;
  void shutdown() noexcept;

  gneiss_application_desc desc_;
  render_internal::render_resource_service resources_;
  gneiss_world world_ = GNEISS_NULL_WORLD;
  std::thread::id owner_thread_;
  std::uint64_t frame_index_ = 0;
  std::uint64_t elapsed_ns_ = 0;
  std::uint64_t previous_time_ns_ = 0;
  bool platform_initialized_ = false;
  bool is_running_ = false;
  bool is_paused_ = false;
  bool should_exit_ = false;
#ifdef GNEISS_HAS_GRANIT_PLATFORM
  std::unique_ptr<granit_platform> granit_platform_;
  std::unique_ptr<granit_render_service> granit_render_service_;
#endif
};

} // namespace gneiss::application_internal

#endif
