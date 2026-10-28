// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RUNTIME_APPLICATION_HPP_
#define GNEISS_RUNTIME_APPLICATION_HPP_

#include "engine/core/input/input_types.hpp"
#include "engine/core/result.hpp"
#include "engine/core/rid.hpp"
#include "engine/core/scene_types.hpp"
#include "engine/function/world/world.hpp"
#include "engine/resource/input/action_map.hpp"
#include "engine/resource/render_resource_factory.hpp"

#include "engine/core/log/log_dispatcher.hpp"
#include "engine/function/render/debug_draw_list.hpp"
#include "engine/function/render/render_asset_loader.hpp"
#include "engine/function/render/render_snapshot.hpp"
#include "engine/function/render/render_statistics.hpp"
#include "engine/function/render/texture_load_service.hpp"
#include "engine/function/render/ui_draw_list.hpp"
#include "engine/function/scene/scene_creation.hpp"
#include "engine/function/scene/scene_mesh_binding.hpp"
#include "runtime/configuration/application_configuration.hpp"
#include "runtime/loading/scene_load_types.hpp"

#include <cstdint>
#include <memory>
#include <span>

namespace gneiss::engine {
class asset_manager;
namespace files {
class service;
}
class render_service;
struct frame_image;
}

namespace gneiss::engine {
class scene_instances;
}
namespace gneiss::engine {
class world;
}

namespace gneiss::runtime {

/** 内部真实运行对象；仅所属线程操作和释放最后一个持有引用。
 * 私有装配存储位于实现文件；此内部模块接口随源码构建，不属于安装 SDK。 */
class GNEISS_API application final {
public:
  /** 校验原生配置并创建已初始化的真实业务对象；失败不改变 output，不创建对外句柄。 */
  [[nodiscard]] static gneiss::engine::result
  create(const runtime::application_configuration& config,
         std::shared_ptr<application>& output) noexcept;
  [[nodiscard]] gneiss::engine::result
  set_view_camera(const gneiss::engine::view_camera* camera) noexcept;
  [[nodiscard]] gneiss::engine::result capture_frame(std::uint32_t width, std::uint32_t height,
                                                     gneiss::engine::frame_image& output) noexcept;
  [[nodiscard]] gneiss::engine::render_queue_stats render_statistics() const noexcept;
  [[nodiscard]] gneiss::engine::result
  attach_task_executor(gneiss::engine::task_executor& executor) noexcept;
  [[nodiscard]] bool has_asset_loading_service() const noexcept;
  [[nodiscard]] gneiss::engine::result create_mesh_node_ready(
      gneiss::scene_instance_id scene, const gneiss::engine::mesh_renderer_node_creation& desc,
      const gneiss::engine::scene_mesh_binding& prepared, scene_node_id& output) noexcept;
  [[nodiscard]] gneiss::engine::result collect_unused_edit_assets() noexcept;
  [[nodiscard]] gneiss::engine::result
  query_ready_asset(const gneiss::engine::render_asset_reload& source,
                    gneiss::engine::render_asset_ref& output) noexcept;
  [[nodiscard]] gneiss::engine::result
  capture_mesh_binding(gneiss::scene_instance_id scene, scene_node_id node,
                       gneiss::engine::scene_mesh_binding& output) noexcept;
  [[nodiscard]] gneiss::engine::result
  replace_mesh_binding(gneiss::scene_instance_id scene, scene_node_id node,
                       const gneiss::engine::scene_mesh_binding& expected,
                       const gneiss::engine::scene_mesh_binding& replacement) noexcept;
  [[nodiscard]] gneiss::engine::result
  prepare_edit_assets(std::span<const gneiss::engine::render_asset_reload> sources,
                      std::uint64_t revision, std::uint64_t& request) noexcept;
  [[nodiscard]] gneiss::engine::result request_scene(std::string_view uri, std::uint64_t session,
                                                     std::uint64_t revision,
                                                     std::uint64_t& request);
  [[nodiscard]] bool scene_progress(runtime::scene_load_progress& progress) const;
  [[nodiscard]] gneiss::engine::result poll_scene(runtime::scene_load_completion& completion,
                                                  bool& ready);
  [[nodiscard]] gneiss::engine::result cancel_scene(std::uint64_t request);
  [[nodiscard]] bool scene_loading() const noexcept;
  [[nodiscard]] gneiss::engine::result
  request_assets(std::span<const gneiss::engine::render_asset_reload> sources,
                 std::uint64_t session, std::uint64_t revision, std::uint64_t& request,
                 bool reload);
  [[nodiscard]] gneiss::engine::result
  poll_assets(gneiss::engine::texture_load_completion& completion, bool& ready);
  [[nodiscard]] bool asset_progress(gneiss::engine::asset_load_progress& progress) const noexcept;
  [[nodiscard]] gneiss::engine::result cancel_assets() noexcept;
  void advance_scene_load();
  [[nodiscard]] bool visual_assets_loading() const noexcept;
  [[nodiscard]] bool can_activate_scene() const noexcept;
  [[nodiscard]] runtime::scene_retirement_statistics scene_retirement() const noexcept;
  [[nodiscard]] gneiss::engine::result activate_scene(std::uint64_t request,
                                                      runtime::scene_load_completion& completion);

private:
  [[nodiscard]] gneiss::engine::result advance_resource_io();
  explicit application(const runtime::application_configuration& config);
  [[nodiscard]] gneiss::engine::result initialize() noexcept;
  void report_visual_completion();

public:
  ~application() noexcept;

  application(const application&) = delete;
  application& operator=(const application&) = delete;
  application(application&&) = delete;
  application& operator=(application&&) = delete;

  /** 所属线程、run 之外请求关闭；拒绝新操作，不等待文件和 CPU 任务。
   * 继续调用 advance_shutdown，返回 not_ready 时让出执行权；终态后可销毁宿主执行器。 */
  [[nodiscard]] gneiss::engine::result request_shutdown() noexcept;
  /** 非阻塞推进文件/CPU 收尾；最终设备销毁仍使用 Render 的既有关闭流程。 */
  [[nodiscard]] gneiss::engine::result advance_shutdown() noexcept;
  [[nodiscard]] gneiss::engine::result shutdown() noexcept;
  [[nodiscard]] gneiss::engine::result run(std::uint64_t max_frame_count) noexcept;
  [[nodiscard]] bool is_owner_thread() const noexcept;
  [[nodiscard]] bool accepts_requests() const noexcept;
  /** 借用活动 World；场景切换或关闭后必须重新获取，禁止解引用旧指针。 */
  [[nodiscard]] gneiss::engine::world* native_world() const noexcept;
  [[nodiscard]] gneiss::engine::render_service& render() noexcept;
  /** 借用应用资源装配上下文；仅所属线程使用。 */
  [[nodiscard]] gneiss::engine::render_resource_factory& resource_factory() noexcept;
  /** 借用应用级资产管理器；仅所属线程使用，有效期不超过 Application。 */
  [[nodiscard]] gneiss::engine::asset_manager& assets() noexcept;
  /** 共享宿主文件服务；由 Application 分发和关闭，消费者只提交/取消自己的请求。 */
  [[nodiscard]] std::shared_ptr<gneiss::engine::files::service> async_files() const noexcept;
  /** 异步重载现有实例的结构；prefab 指定是否更新该实例中的同源 Prefab。 */
  [[nodiscard]] engine::result request_scene_reload(scene_instance_id instance,
                                                    std::string_view uri,
                                                    bool prefab = false) noexcept;
  [[nodiscard]] engine::result poll_scene_reload(engine::result& result, bool& ready) noexcept;
  [[nodiscard]] gneiss::engine::scene_instances* scenes() noexcept;
  [[nodiscard]] const gneiss::engine::keyboard_state& keyboard_state() const noexcept;
  [[nodiscard]] const gneiss::engine::pointer_state& pointer_state() const noexcept;
  [[nodiscard]] gneiss::engine::result poll_input(gneiss::engine::input_event& out_event) noexcept;
  [[nodiscard]] gneiss::engine::result get_window_size(std::uint32_t& out_width,
                                                       std::uint32_t& out_height) const noexcept;
  /** 仅所属线程；成功只表示已接受，主循环在 update 前应用。未启用后端返回 UNSUPPORTED。 */
  [[nodiscard]] gneiss::engine::result begin_action_map(std::string_view uri) noexcept;
  /** NOT_READY 表示在途；成功表示已应用。失败保留原映射。 */
  [[nodiscard]] gneiss::engine::result action_map_status() const noexcept;
  [[nodiscard]] gneiss::engine::result cancel_action_map() noexcept;
  [[nodiscard]] gneiss::engine::result
  find_action(std::string_view name, gneiss::engine::action_id& out_action) const noexcept;
  [[nodiscard]] gneiss::engine::result
  get_action_state(gneiss::engine::action_id action,
                   gneiss::engine::action_state& out_state) const noexcept;
  [[nodiscard]] bool can_submit_draw_lists() const noexcept;
  [[nodiscard]] gneiss::engine::result
  submit_ui_draw_list(const gneiss::engine::ui_draw_view& desc) noexcept;
  [[nodiscard]] gneiss::engine::result
  submit_debug_draw_list(std::span<const gneiss::engine::debug_line> lines) noexcept;
  void report(gneiss::engine::severity severity, gneiss::engine::category category,
              gneiss::engine::result result, std::string_view module,
              std::string_view message) noexcept;
  [[nodiscard]] gneiss::engine::result
  submit_log(const gneiss::engine::message_view& message) noexcept;
  void request_exit() noexcept;
  void set_paused(bool value) noexcept;

private:
  [[nodiscard]] std::uint64_t now_ns() const;
  [[nodiscard]] gneiss::engine::result poll_events(bool& out_should_close) noexcept;
  [[nodiscard]] gneiss::engine::result render_frame() noexcept;
  [[nodiscard]] gneiss::engine::texture_load_service* texture_service() noexcept;
  struct storage;
  std::unique_ptr<storage> storage_;
};

} // namespace gneiss::runtime

#endif
