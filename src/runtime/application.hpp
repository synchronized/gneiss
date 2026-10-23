// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RUNTIME_APPLICATION_HPP_
#define GNEISS_RUNTIME_APPLICATION_HPP_

#include "engine/core/input/input_types.hpp"
#include "engine/resource/types/action_map.hpp"
#include <gneiss/engine/application.h>

#include "engine/core/log/log_dispatcher.hpp"
#include "engine/function/render/debug_draw_list.hpp"
#include "engine/function/render/render_asset_loader.hpp"
#include "engine/function/render/render_snapshot.hpp"
#include "engine/function/render/render_statistics.hpp"
#include "engine/function/render/texture_load_service.hpp"
#include "engine/function/render/ui_draw_list.hpp"
#include "engine/function/scene/scene_creation.hpp"
#include "engine/function/scene/scene_mesh_binding.hpp"
#include "engine/resource/loaders/prefab_asset_loader.hpp"
#include "engine/resource/vfs/virtual_file_system.hpp"
#include "runtime/configuration/application_configuration.hpp"
#include "runtime/loading/scene_load_types.hpp"

#include <cstdint>
#include <memory>
#include <span>

namespace gneiss::engine::render {
class render_resource_service;
struct frame_image;
}

namespace gneiss::engine::scene {
class scene_instance_service;
}
namespace gneiss::engine::world {
class world;
}

namespace gneiss::runtime {

/** 内部真实运行对象；仅所属线程操作和释放最后一个持有引用。
 * 私有装配存储位于实现文件；此内部模块接口随源码构建，不属于安装 SDK。 */
class GNEISS_API application final {
public:
  /** 校验原生配置并创建已初始化的真实业务对象；失败不改变 output，不创建对外句柄。 */
  [[nodiscard]] static gneiss_result create(const runtime::application_configuration& config,
                                            std::shared_ptr<application>& output) noexcept;
  [[nodiscard]] const engine::resource::virtual_file_system& asset_files() const noexcept;
  [[nodiscard]] gneiss_result set_view_camera(const engine::render::view_camera* camera) noexcept;
  [[nodiscard]] gneiss_result capture_frame(std::uint32_t width, std::uint32_t height,
                                            engine::render::frame_image& output) noexcept;
  [[nodiscard]] engine::render::render_queue_stats render_statistics() const noexcept;
  [[nodiscard]] gneiss_result
  attach_task_executor(engine::core::tasks::task_executor& executor) noexcept;
  [[nodiscard]] bool has_asset_loading_service() const noexcept;
  [[nodiscard]] gneiss_result create_mesh_node_ready(
      gneiss_scene_instance scene, const engine::scene::mesh_renderer_node_creation& desc,
      const engine::scene::scene_mesh_binding& prepared, scene_node_id& output) noexcept;
  [[nodiscard]] gneiss_result collect_unused_edit_assets() noexcept;
  [[nodiscard]] gneiss_result
  query_ready_asset(const engine::render::render_asset_reload& source,
                    engine::render::render_asset_lease& output) noexcept;
  [[nodiscard]] gneiss_result
  capture_mesh_binding(gneiss_scene_instance scene, scene_node_id node,
                       engine::scene::scene_mesh_binding& output) noexcept;
  [[nodiscard]] gneiss_result
  replace_mesh_binding(gneiss_scene_instance scene, scene_node_id node,
                       const engine::scene::scene_mesh_binding& expected,
                       const engine::scene::scene_mesh_binding& replacement) noexcept;
  [[nodiscard]] gneiss_result
  prepare_edit_assets(std::span<const engine::render::render_asset_reload> sources,
                      std::uint64_t revision, std::uint64_t& request) noexcept;
  [[nodiscard]] gneiss_result request_scene(std::string_view uri, std::uint64_t session,
                                            std::uint64_t revision, std::uint64_t& request);
  [[nodiscard]] bool scene_progress(runtime::scene_load_progress& progress) const;
  [[nodiscard]] gneiss_result poll_scene(runtime::scene_load_completion& completion, bool& ready);
  [[nodiscard]] gneiss_result cancel_scene(std::uint64_t request);
  [[nodiscard]] bool scene_loading() const noexcept;
  [[nodiscard]] gneiss_result
  request_assets(std::span<const engine::render::render_asset_reload> sources,
                 std::uint64_t session, std::uint64_t revision, std::uint64_t& request,
                 bool reload);
  [[nodiscard]] gneiss_result poll_assets(engine::render::texture_load_completion& completion,
                                          bool& ready);
  [[nodiscard]] bool asset_progress(engine::render::asset_load_progress& progress) const noexcept;
  [[nodiscard]] gneiss_result cancel_assets() noexcept;
  void advance_scene_load();
  [[nodiscard]] bool visual_assets_loading() const noexcept;
  [[nodiscard]] bool can_activate_scene() const noexcept;
  [[nodiscard]] runtime::scene_retirement_statistics scene_retirement() const noexcept;
  [[nodiscard]] gneiss_result activate_scene(std::uint64_t request,
                                             runtime::scene_load_completion& completion);

private:
  explicit application(const runtime::application_configuration& config);
  [[nodiscard]] gneiss_result initialize() noexcept;
  void report_visual_completion();

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
  [[nodiscard]] engine::world::world* native_world() const noexcept;
  [[nodiscard]] engine::render::render_resource_service& resources() noexcept;
  [[nodiscard]] engine::resource::prefab_asset_loader& prefab_loader() noexcept;
  [[nodiscard]] gneiss_result
  reload_render_assets(std::span<const engine::render::render_asset_reload> assets) noexcept;
  [[nodiscard]] gneiss_result reload_scene(gneiss_scene_instance instance,
                                           std::string_view uri) noexcept;
  [[nodiscard]] gneiss_result reload_prefab(gneiss_scene_instance instance,
                                            std::string_view uri) noexcept;
  [[nodiscard]] engine::scene::scene_instance_service* scenes() noexcept;
  [[nodiscard]] const engine::core::input::keyboard_state& keyboard_state() const noexcept;
  [[nodiscard]] const engine::core::input::pointer_state& pointer_state() const noexcept;
  [[nodiscard]] gneiss_result poll_input(engine::core::input::input_event& out_event) noexcept;
  [[nodiscard]] gneiss_result get_window_size(std::uint32_t& out_width,
                                              std::uint32_t& out_height) const noexcept;
  [[nodiscard]] gneiss_result load_action_map(std::string_view uri) noexcept;
  /** 仅所属线程；成功只表示已接受，主循环在 update 前应用。未启用后端返回 UNSUPPORTED。 */
  [[nodiscard]] gneiss_result begin_action_map(std::string_view uri) noexcept;
  /** NOT_READY 表示在途；成功表示已应用。失败保留原映射。 */
  [[nodiscard]] gneiss_result action_map_status() const noexcept;
  [[nodiscard]] gneiss_result cancel_action_map() noexcept;
  [[nodiscard]] gneiss_result
  find_action(std::string_view name, engine::core::input::action_id& out_action) const noexcept;
  [[nodiscard]] gneiss_result
  get_action_state(engine::core::input::action_id action,
                   engine::core::input::action_state& out_state) const noexcept;
  [[nodiscard]] bool can_submit_draw_lists() const noexcept;
  [[nodiscard]] gneiss_result
  submit_ui_draw_list(const engine::render::ui_draw_view& desc) noexcept;
  [[nodiscard]] gneiss_result
  submit_debug_draw_list(std::span<const engine::render::debug_line> lines) noexcept;
  void report(gneiss_application handle, engine::core::diagnostics::severity severity,
              engine::core::diagnostics::category category, gneiss_result result,
              std::string_view module, std::string_view message) noexcept;
  [[nodiscard]] gneiss_result submit_log(const engine::core::log::message_view& message) noexcept;
  void request_exit() noexcept;
  void set_paused(bool value) noexcept;

private:
  [[nodiscard]] std::uint64_t now_ns() const;
  [[nodiscard]] gneiss_result poll_events(bool& out_should_close) noexcept;
  [[nodiscard]] gneiss_result render_frame() noexcept;
  [[nodiscard]] engine::render::texture_load_service* texture_service() noexcept;
  struct storage;
  std::unique_ptr<storage> storage_;
};

} // namespace gneiss::runtime

#endif
