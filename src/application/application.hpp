// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RUNTIME_APPLICATION_HPP_
#define GNEISS_RUNTIME_APPLICATION_HPP_

#include "engine/core/input/input_types.hpp"
#include "engine/core/result.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/asset/asset_manager.hpp"
#include "engine/function/scene/scene_types.hpp"
#include "engine/function/scene/world.hpp"
#include "engine/resource/input/action_map.hpp"

#include "application/application_configuration.hpp"
#include "engine/core/log/log_dispatcher.hpp"
#include "engine/function/asset/asset_batch_request.hpp"
#include "engine/function/asset/asset_batch_types.hpp"
#include "engine/function/render/debug_draw_list.hpp"
#include "engine/function/render/render_snapshot.hpp"
#include "engine/function/render/render_statistics.hpp"
#include "engine/function/render/ui_draw_list.hpp"

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

namespace gneiss::engine {}
namespace gneiss::engine {
class world;
}

namespace gneiss::engine {
class resource_load_group;
}

namespace gneiss::runtime {

/** 内部真实运行对象；仅所属线程操作和释放最后一个持有引用。
 * 私有装配存储位于实现文件；此内部模块接口随源码构建，不属于安装 SDK。 */
class application final {
public:
  /** 返回已初始化对象的拥有引用；失败返回错误并清理部分初始化的资源。
   * 创建、运行及最后释放均限所属线程，不创建对外句柄。 */
  [[nodiscard]] static GNEISS_API engine::result<std::shared_ptr<application>>
  create(const application_configuration& config);
  [[nodiscard]] GNEISS_API gneiss::engine::error
  set_view_camera(const gneiss::engine::view_camera* camera);
  [[nodiscard]] GNEISS_API gneiss::engine::error
  capture_frame(std::uint32_t width, std::uint32_t height, gneiss::engine::frame_image& output);
  [[nodiscard]] GNEISS_API gneiss::engine::render_queue_stats render_statistics() const;
  [[nodiscard]] GNEISS_API gneiss::engine::error
  attach_task_executor(gneiss::engine::task_executor& executor);
  [[nodiscard]] GNEISS_API bool has_asset_loading_service() const noexcept;
  /** 多资源编辑批次；普通单资产直接使用 assets().load<T>()。 */
  [[nodiscard]] GNEISS_API engine::result<engine::asset_batch_request>
  load_assets(std::span<const engine::render_resource_source> sources, bool reload = false);
  /** 所属线程查询共享资产批次是否可提交；场景切换策略由宿主维护。 */
  [[nodiscard]] GNEISS_API bool can_load_assets() const noexcept;

private:
  [[nodiscard]] engine::error advance_loading();
  engine::resource_load_group* loading_assets() noexcept;
  [[nodiscard]] GNEISS_API gneiss::engine::error advance_resource_io();
  explicit GNEISS_API application(const runtime::application_configuration& config);
  [[nodiscard]] GNEISS_API gneiss::engine::error initialize();

public:
  GNEISS_API ~application() noexcept;

  application(const application&) = delete;
  application& operator=(const application&) = delete;
  application(application&&) = delete;
  application& operator=(application&&) = delete;

  /** 所属线程、run 之外请求关闭；拒绝新操作，不等待文件和 CPU 任务。
   * 继续调用 advance_shutdown，返回 not_ready 时让出执行权；终态后可销毁宿主执行器。 */
  [[nodiscard]] GNEISS_API gneiss::engine::error request_shutdown();
  /** 非阻塞推进文件/CPU 收尾；最终设备销毁仍使用 Render 的既有关闭流程。 */
  [[nodiscard]] GNEISS_API gneiss::engine::error advance_shutdown();
  [[nodiscard]] GNEISS_API gneiss::engine::error shutdown();
  [[nodiscard]] GNEISS_API gneiss::engine::error run(std::uint64_t max_frame_count);
  [[nodiscard]] GNEISS_API bool is_owner_thread() const noexcept;
  [[nodiscard]] GNEISS_API bool accepts_requests() const noexcept;
  /** 借用宿主 World；普通场景切换保持地址稳定，关闭后禁止解引用。 */
  [[nodiscard]] GNEISS_API gneiss::engine::world* native_world() const noexcept;
  [[nodiscard]] GNEISS_API gneiss::engine::render_service& render() noexcept;
  /** 借用应用级资产管理器；仅所属线程使用，有效期不超过 Application。 */
  [[nodiscard]] GNEISS_API gneiss::engine::asset_manager& assets() noexcept;
  /** 共享宿主文件服务；由 Application 分发和关闭，消费者只提交/取消自己的请求。 */
  [[nodiscard]] GNEISS_API std::shared_ptr<gneiss::engine::files::service>
  async_files() const noexcept;
  [[nodiscard]] GNEISS_API const gneiss::engine::keyboard_state& keyboard_state() const noexcept;
  [[nodiscard]] GNEISS_API const gneiss::engine::pointer_state& pointer_state() const noexcept;
  [[nodiscard]] GNEISS_API gneiss::engine::error
  poll_input(gneiss::engine::input_event& out_event) noexcept;
  [[nodiscard]] GNEISS_API gneiss::engine::error
  get_window_size(std::uint32_t& out_width, std::uint32_t& out_height) const noexcept;
  /** 仅所属线程；成功只表示已接受，主循环在 update 前应用。未启用后端返回 UNSUPPORTED。 */
  [[nodiscard]] GNEISS_API gneiss::engine::error begin_action_map(std::string_view uri);
  /** NOT_READY 表示在途；成功表示已应用。失败保留原映射。 */
  [[nodiscard]] GNEISS_API gneiss::engine::error action_map_status() const noexcept;
  [[nodiscard]] GNEISS_API gneiss::engine::error cancel_action_map() noexcept;
  [[nodiscard]] GNEISS_API gneiss::engine::error
  find_action(std::string_view name, gneiss::engine::action_id& out_action) const noexcept;
  [[nodiscard]] GNEISS_API gneiss::engine::error
  get_action_state(gneiss::engine::action_id action,
                   gneiss::engine::action_state& out_state) const noexcept;
  [[nodiscard]] GNEISS_API bool can_submit_draw_lists() const noexcept;
  [[nodiscard]] GNEISS_API gneiss::engine::error
  submit_ui_draw_list(const gneiss::engine::ui_draw_view& desc);
  [[nodiscard]] GNEISS_API gneiss::engine::error
  submit_debug_draw_list(std::span<const gneiss::engine::debug_line> lines);
  GNEISS_API void report(gneiss::engine::severity severity, gneiss::engine::category category,
                         gneiss::engine::error result, std::string_view module,
                         std::string_view message);
  [[nodiscard]] GNEISS_API gneiss::engine::error
  submit_log(const gneiss::engine::message_view& message);
  GNEISS_API void request_exit() noexcept;
  GNEISS_API void set_paused(bool value) noexcept;

private:
  [[nodiscard]] GNEISS_API std::uint64_t now_ns() const;
  [[nodiscard]] GNEISS_API gneiss::engine::error poll_events(bool& out_should_close);
  [[nodiscard]] GNEISS_API gneiss::engine::error render_frame();

  struct storage;
  std::unique_ptr<storage> storage_;
};

} // namespace gneiss::runtime

#endif
