// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_ENGINE_APPLICATION_HPP_
#define GNEISS_ENGINE_APPLICATION_HPP_

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

#include <atomic>
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

namespace gneiss::engine {

/** 内部真实运行对象；仅所属线程操作和释放最后一个持有引用。
 * 私有装配存储位于实现文件；此内部模块接口随源码构建，不属于安装 SDK。 */
class GNEISS_API application final {
public:
  /** 校验原生配置并创建已初始化的真实业务对象；失败不改变 output，不创建对外句柄。 */
  [[nodiscard]] static gneiss_result
  create(const application_internal::application_configuration& config,
         std::shared_ptr<application>& output) noexcept;
  [[nodiscard]] const asset_internal::virtual_file_system& asset_files() const noexcept;
  [[nodiscard]] gneiss_result set_view_camera(const render_internal::view_camera* camera) noexcept;
  [[nodiscard]] gneiss_result capture_frame(std::uint32_t width, std::uint32_t height,
                                            render_internal::frame_image& output) noexcept;
  [[nodiscard]] render_internal::render_queue_stats render_statistics() const noexcept;
  [[nodiscard]] gneiss_result attach_task_executor(tasks::task_executor& executor) noexcept;
  [[nodiscard]] render_internal::texture_load_service* texture_service() noexcept;
  [[nodiscard]] gneiss_result
  accept_edit_assets(const render_internal::texture_load_completion& completion,
                     const std::function<gneiss_result()>& accept);
  [[nodiscard]] gneiss_result request_scene(std::string_view uri, std::uint64_t session,
                                            std::uint64_t revision, std::uint64_t& request);
  [[nodiscard]] bool scene_progress(application_internal::scene_load_progress& progress) const;
  [[nodiscard]] gneiss_result poll_scene(application_internal::scene_load_completion& completion,
                                         bool& ready);
  [[nodiscard]] gneiss_result cancel_scene(std::uint64_t request);
  [[nodiscard]] bool scene_loading() const noexcept;
  [[nodiscard]] gneiss_result
  request_assets(std::span<const render_internal::render_asset_reload> sources,
                 std::uint64_t session, std::uint64_t revision, std::uint64_t& request,
                 bool reload);
  [[nodiscard]] gneiss_result poll_assets(render_internal::texture_load_completion& completion,
                                          bool& ready);
  void advance_scene_load();
  [[nodiscard]] application_internal::scene_retirement_statistics scene_retirement() const noexcept;
  [[nodiscard]] gneiss_result
  activate_scene(std::uint64_t request, application_internal::scene_load_completion& completion);

private:
  explicit application(const application_internal::application_configuration& config);
  [[nodiscard]] gneiss_result initialize() noexcept;

public:
  ~application() noexcept;

  application(const application&) = delete;
  application& operator=(const application&) = delete;
  application(application&&) = delete;
  application& operator=(application&&) = delete;

  [[nodiscard]] gneiss_result shutdown(gneiss_application handle) noexcept;
  [[nodiscard]] gneiss_result run(gneiss_application handle,
                                  std::uint64_t max_frame_count) noexcept;
  [[nodiscard]] bool is_owner_thread() const noexcept;
  [[nodiscard]] bool accepts_requests() const noexcept;
  [[nodiscard]] gneiss_world world() const noexcept;
  [[nodiscard]] render_internal::render_resource_service& resources() noexcept;
  [[nodiscard]] render_internal::render_asset_loader& asset_loader() noexcept;
  [[nodiscard]] scene_internal::prefab_asset_loader& prefab_loader() noexcept;
  [[nodiscard]] gneiss_result
  reload_render_assets(std::span<const render_internal::render_asset_reload> assets) noexcept;
  [[nodiscard]] gneiss_result reload_scene(gneiss_scene_instance instance,
                                           std::string_view uri) noexcept;
  [[nodiscard]] gneiss_result reload_prefab(gneiss_scene_instance instance,
                                            std::string_view uri) noexcept;
  [[nodiscard]] scene_internal::scene_instance_service* scenes() noexcept;
  [[nodiscard]] const gneiss_keyboard_state& keyboard_state() const noexcept;
  [[nodiscard]] const gneiss_pointer_state& pointer_state() const noexcept;
  [[nodiscard]] gneiss_result poll_input(gneiss_input_event& out_event) noexcept;
  [[nodiscard]] gneiss_result get_window_size(std::uint32_t& out_width,
                                              std::uint32_t& out_height) const noexcept;
  [[nodiscard]] gneiss_result load_action_map(std::string_view uri) noexcept;
  [[nodiscard]] gneiss_result find_action(std::string_view name,
                                          input_internal::action_id& out_action) const noexcept;
  [[nodiscard]] gneiss_result get_action_state(input_internal::action_id action,
                                               input_internal::action_state& out_state) const noexcept;
  [[nodiscard]] bool can_submit_draw_lists() const noexcept;
  [[nodiscard]] gneiss_result
  submit_ui_draw_list(const render_internal::ui_draw_view& desc) noexcept;
  [[nodiscard]] gneiss_result
  submit_debug_draw_list(std::span<const gneiss_debug_line> lines) noexcept;
  void report(gneiss_application handle, std::uint32_t severity, std::uint32_t category,
              gneiss_result result, std::string_view module, std::string_view message) noexcept;
  [[nodiscard]] gneiss_result submit_log(const log_internal::message_view& message) noexcept;
  void request_exit() noexcept;
  void set_paused(bool value) noexcept;

private:
  [[nodiscard]] std::uint64_t now_ns() const noexcept;
  [[nodiscard]] gneiss_result poll_events(bool& out_should_close) noexcept;
  [[nodiscard]] gneiss_result render_frame() noexcept;
  struct storage;
  std::unique_ptr<storage> storage_;
};

} // namespace gneiss::engine

#endif
