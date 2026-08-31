// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPLICATION_APPLICATION_STATE_H_
#define GNEISS_APPLICATION_APPLICATION_STATE_H_

#include <gneiss/application.h>
#include <gneiss/input.h>

#include "asset/resource_cache.h"
#include "asset/virtual_file_system.h"
#include "input/input_service.h"
#include "log/log_dispatcher.h"
#include "render/debug_draw_list.h"
#include "render/render_asset_loader.h"
#include "render/render_resource_service.h"
#include "render/ui_draw_list.h"
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
  [[nodiscard]] gneiss_result shutdown(gneiss_application handle) noexcept;
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
    return input_.keyboard();
  }
  [[nodiscard]] const gneiss_pointer_state& pointer_state() const noexcept {
    return input_.pointer();
  }
  [[nodiscard]] gneiss_result poll_input(gneiss_input_event& out_event) noexcept;
  [[nodiscard]] gneiss_result load_action_map(std::string_view uri) noexcept;
  [[nodiscard]] gneiss_result find_action(std::string_view name,
                                          gneiss_action& out_action) const noexcept;
  [[nodiscard]] gneiss_result get_action_state(gneiss_action action,
                                               gneiss_action_state& out_state) const noexcept;
  [[nodiscard]] gneiss_result submit_ui_draw_list(const gneiss_ui_draw_list_desc& desc) noexcept;
  [[nodiscard]] gneiss_result
  submit_debug_draw_list(const gneiss_debug_draw_list_desc& desc) noexcept;
  void report(gneiss_application handle, std::uint32_t severity, std::uint32_t category,
              gneiss_result result, std::string_view module, std::string_view message) noexcept;
  [[nodiscard]] gneiss_result submit_log(gneiss_application handle,
                                         const gneiss_log_message& message,
                                         std::string_view source = "application") noexcept;
  void request_exit() noexcept { should_exit_ = true; }
  void set_paused(bool value) noexcept { is_paused_ = value; }

private:
  [[nodiscard]] std::uint64_t now_ns() const noexcept;
  [[nodiscard]] gneiss_result poll_events(bool& out_should_close) noexcept;
#ifdef GNEISS_HAS_GRANIT_PLATFORM
  [[nodiscard]] gneiss_result render_frame() noexcept;
#endif
  gneiss_application_desc desc_;
  render_internal::render_resource_service resources_;
  render_internal::ui_draw_list ui_draw_list_;
  render_internal::debug_draw_list debug_draw_list_;
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
  bool is_updating_ = false;
  input_internal::input_service input_;
  std::unique_ptr<log_internal::log_dispatcher> log_dispatcher_;
#ifdef GNEISS_HAS_GRANIT_PLATFORM
  std::unique_ptr<granit_platform> granit_platform_;
  std::unique_ptr<granit_render_service> granit_render_service_;
#endif
};

} // namespace gneiss::application_internal

#endif
