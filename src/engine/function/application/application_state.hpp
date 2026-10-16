// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPLICATION_APPLICATION_STATE_HPP_
#define GNEISS_APPLICATION_APPLICATION_STATE_HPP_

#include <gneiss/engine/application.h>
#include <gneiss/engine/input.h>

#include "engine/asset/virtual_file_system.hpp"
#include "engine/core/diagnostics/loop_timing.hpp"
#include "engine/core/log/log_dispatcher.hpp"
#include "engine/function/application/application_configuration.hpp"
#include "engine/function/application/scene_load_service.hpp"
#include "engine/function/application/scene_session.hpp"
#include "engine/function/input/input_service.hpp"
#include "engine/function/render/debug_draw_list.hpp"
#include "engine/function/render/render_asset_loader.hpp"
#include "engine/function/render/render_executor.hpp"
#include "engine/function/render/render_resource_service.hpp"
#include "engine/function/render/render_snapshot.hpp"
#include "engine/function/render/texture_load_service.hpp"
#include "engine/function/render/ui_draw_list.hpp"
#include "engine/function/scene/prefab_asset_loader.hpp"
#include "engine/function/scene/scene_instance_service.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <thread>

namespace gneiss::render_internal {
class granit_render_service;
}

namespace gneiss::platform {
class granit_platform;
}

namespace gneiss::io_internal {
class uv_loop_executor;
}
namespace gneiss::asset_internal {
class native_file_system;
}

namespace gneiss::application_internal {

class application_state final {
public:
  [[nodiscard]] const asset_internal::virtual_file_system& asset_files() const noexcept {
    return asset_file_system_;
  }
  [[nodiscard]] gneiss_result set_view_camera(const render_internal::view_camera* camera) noexcept;
  [[nodiscard]] gneiss_result capture_frame(std::uint32_t width, std::uint32_t height,
                                            render_internal::frame_image& output) noexcept;
  [[nodiscard]] render_internal::render_queue_stats render_statistics() const noexcept;
  [[nodiscard]] gneiss_result attach_task_executor(tasks::task_executor& executor) noexcept;
  [[nodiscard]] render_internal::texture_load_service* texture_service() noexcept {
    return scene_session_.textures();
  }
  [[nodiscard]] gneiss_result
  accept_edit_assets(const render_internal::texture_load_completion& completion,
                     const std::function<gneiss_result()>& accept) {
    return scene_session_.accept_edit_assets(completion, accept);
  }
  [[nodiscard]] gneiss_result request_scene(std::string_view uri, std::uint64_t session,
                                            std::uint64_t revision, std::uint64_t& request) {
    return scene_session_.request_scene(uri, session, revision, request);
  }
  [[nodiscard]] bool scene_progress(scene_load_progress& progress) const {
    return scene_session_.scene_progress(progress);
  }
  [[nodiscard]] gneiss_result poll_scene(scene_load_completion& completion, bool& ready) {
    return scene_session_.poll_scene(completion, ready);
  }
  [[nodiscard]] gneiss_result cancel_scene(std::uint64_t request) {
    return scene_session_.cancel_scene(request);
  }
  [[nodiscard]] bool scene_loading() const noexcept { return scene_session_.loading(); }
  [[nodiscard]] gneiss_result
  request_assets(std::span<const render_internal::render_asset_reload> sources,
                 std::uint64_t session, std::uint64_t revision, std::uint64_t& request,
                 bool reload) {
    return scene_session_.request_assets(sources, session, revision, request, reload);
  }
  [[nodiscard]] gneiss_result poll_assets(render_internal::texture_load_completion& completion,
                                          bool& ready) {
    return scene_session_.poll_assets(completion, ready);
  }
  void advance_scene_load() { scene_session_.advance(); }
  [[nodiscard]] scene_retirement_statistics scene_retirement() const noexcept {
    auto value = scene_session_.retirement();
    value.live_resources = resources_.live_resource_count();
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
  [[nodiscard]] gneiss_world world() const noexcept { return scene_session_.world(); }
  [[nodiscard]] render_internal::render_resource_service& resources() noexcept {
    return resources_;
  }
  [[nodiscard]] render_internal::render_asset_loader& asset_loader() noexcept {
    return scene_session_.assets();
  }
  [[nodiscard]] gneiss_result
  reload_render_assets(std::span<const render_internal::render_asset_reload> assets) noexcept;
  [[nodiscard]] gneiss_result reload_scene(gneiss_scene_instance instance,
                                           std::string_view uri) noexcept;
  [[nodiscard]] gneiss_result reload_prefab(gneiss_scene_instance instance,
                                            std::string_view uri) noexcept;
  [[nodiscard]] scene_internal::scene_instance_service* scenes() noexcept {
    return scene_session_.scenes();
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
  std::shared_ptr<core::progress_notification> loop_notification_;
  application_configuration config_;
  render_internal::render_resource_service resources_;
  render_internal::ui_draw_list ui_draw_list_;
  render_internal::debug_draw_list debug_draw_list_;
  std::optional<render_internal::view_camera> view_camera_;
#ifdef GNEISS_ASSET_LIBUV
  std::unique_ptr<io_internal::uv_loop_executor> asset_io_executor_;
  std::shared_ptr<asset_internal::native_file_system> native_assets_;
#endif
  asset_internal::virtual_file_system asset_file_system_;
  scene_session scene_session_;
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
