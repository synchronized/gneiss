// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPLICATION_APPLICATION_STATE_HPP_
#define GNEISS_APPLICATION_APPLICATION_STATE_HPP_

#include <gneiss/application.h>
#include <gneiss/input.h>

#include "engine/asset/virtual_file_system.hpp"
#include "engine/core/log/log_dispatcher.hpp"
#include "engine/function/application/application_configuration.hpp"
#include "engine/function/application/application_scene_state.hpp"
#include "engine/function/application/scene_load_service.hpp"
#include "engine/function/input/input_service.hpp"
#include "engine/function/render/debug_draw_list.hpp"
#include "engine/function/render/render_asset_loader.hpp"
#include "engine/function/render/render_executor.hpp"
#include "engine/function/render/render_resource_service.hpp"
#include "engine/function/render/texture_load_service.hpp"
#include "engine/function/render/ui_draw_list.hpp"
#include "engine/function/scene/prefab_asset_loader.hpp"
#include "engine/function/scene/scene_instance_service.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <thread>

namespace gneiss::render_internal {
class granit_render_service;
}

namespace gneiss::platform {
class granit_platform;
}

namespace gneiss::application_internal {

class application_state final {
public:
  [[nodiscard]] gneiss_result capture_frame(std::uint32_t width, std::uint32_t height,
                                            render_internal::frame_image& output) noexcept;
  [[nodiscard]] render_internal::render_queue_stats render_statistics() const noexcept;
  [[nodiscard]] gneiss_result attach_task_executor(tasks::task_executor& executor) noexcept;
  [[nodiscard]] render_internal::texture_load_service* texture_service() noexcept {
    return texture_service_.get();
  }

  [[nodiscard]] scene_load_service* scene_service() noexcept { return scene_service_.get(); }
  [[nodiscard]] bool can_start_scene_load() const noexcept {
    return !retired_scene_ && texture_service_ && !texture_service_->busy();
  }
  [[nodiscard]] bool scene_loading() const noexcept {
    return scene_service_ && scene_service_->busy();
  }
  void advance_scene_load() {
    if (retired_scene_) {
      const auto start = std::chrono::steady_clock::now();
      retired_scene_.reset();
      retirement_.last_ms =
          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
              .count();
      ++retirement_.retired_domains;
    }
    if (scene_service_) {
      scene_service_->advance();
    }
  }
  [[nodiscard]] scene_retirement_statistics scene_retirement() const noexcept {
    auto value = retirement_;
    value.live_resources = resources_.live_resource_count();
    value.pending = retired_scene_ != nullptr;
    return value;
  }
  [[nodiscard]] gneiss_result activate_scene(std::uint64_t request,
                                             scene_load_completion& completion);

  explicit application_state(const application_configuration& config) noexcept;
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
  [[nodiscard]] gneiss_world world() const noexcept {
    return active_scene_ ? active_scene_->world : GNEISS_NULL_WORLD;
  }
  [[nodiscard]] render_internal::render_resource_service& resources() noexcept {
    return resources_;
  }
  [[nodiscard]] render_internal::render_asset_loader& asset_loader() noexcept {
    return active_scene_->assets;
  }
  [[nodiscard]] gneiss_result
  reload_render_assets(std::span<const render_internal::render_asset_reload> assets) noexcept;
  [[nodiscard]] gneiss_result reload_scene(gneiss_scene_instance instance,
                                           std::string_view uri) noexcept;
  [[nodiscard]] gneiss_result reload_prefab(gneiss_scene_instance instance,
                                            std::string_view uri) noexcept;
  [[nodiscard]] scene_internal::scene_instance_service* scenes() noexcept {
    return active_scene_ ? active_scene_->scenes.get() : nullptr;
  }
  [[nodiscard]] const gneiss_keyboard_state& keyboard_state() const noexcept {
    return input_.keyboard();
  }
  [[nodiscard]] const gneiss_pointer_state& pointer_state() const noexcept {
    return input_.pointer();
  }
  [[nodiscard]] gneiss_result poll_input(gneiss_input_event& out_event) noexcept;
  [[nodiscard]] gneiss_result get_window_size(std::uint32_t& out_width,
                                              std::uint32_t& out_height) const noexcept;
  [[nodiscard]] gneiss_result load_action_map(std::string_view uri) noexcept;
  [[nodiscard]] gneiss_result find_action(std::string_view name,
                                          gneiss_action& out_action) const noexcept;
  [[nodiscard]] gneiss_result get_action_state(gneiss_action action,
                                               gneiss_action_state& out_state) const noexcept;
  [[nodiscard]] bool can_submit_draw_lists() const noexcept { return is_updating_; }
  [[nodiscard]] gneiss_result
  submit_ui_draw_list(const render_internal::ui_draw_view& desc) noexcept;
  [[nodiscard]] gneiss_result
  submit_debug_draw_list(std::span<const gneiss_debug_line> lines) noexcept;
  void report(gneiss_application handle, std::uint32_t severity, std::uint32_t category,
              gneiss_result result, std::string_view module, std::string_view message) noexcept;
  [[nodiscard]] gneiss_result submit_log(const log_internal::message_view& message) noexcept;
  void request_exit() noexcept { should_exit_ = true; }
  void set_paused(bool value) noexcept { is_paused_ = value; }

private:
  [[nodiscard]] std::uint64_t now_ns() const noexcept;
  [[nodiscard]] gneiss_result poll_events(bool& out_should_close) noexcept;
#ifdef GNEISS_HAS_GRANIT_PLATFORM
  [[nodiscard]] gneiss_result render_frame() noexcept;
#endif
  application_configuration config_;
  render_internal::render_resource_service resources_;
  render_internal::ui_draw_list ui_draw_list_;
  render_internal::debug_draw_list debug_draw_list_;
  asset_internal::virtual_file_system asset_file_system_;
  std::unique_ptr<application_scene_state> active_scene_;
  std::unique_ptr<application_scene_state> retired_scene_;
  scene_retirement_statistics retirement_;
  std::unique_ptr<scene_load_service> scene_service_;
  std::unique_ptr<render_internal::texture_load_service> texture_service_;
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
  std::unique_ptr<platform::granit_platform> granit_platform_;
  std::unique_ptr<render_internal::granit_render_service> granit_render_service_;
#endif
};

} // namespace gneiss::application_internal

#endif
