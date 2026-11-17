// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RUNTIME_APPLICATION_HPP_
#define GNEISS_RUNTIME_APPLICATION_HPP_

#include <gneiss/engine/core/input/types.hpp>
#include <gneiss/engine/core/types/rid.hpp>
#include <gneiss/engine/function/asset/asset_manager.hpp>
#include <gneiss/engine/function/input/action.hpp>
#include <gneiss/engine/function/scene/scene_types.hpp>
#include <gneiss/engine/function/scene/world.hpp>
#include <gneiss/engine/resource/input/action_map.hpp>
#include <gneiss/engine/result.hpp>

#include <gneiss/engine/core/logging/record.hpp>
#include <gneiss/engine/function/application/configuration.hpp>
#include <gneiss/engine/function/asset/asset_batch_request.hpp>
#include <gneiss/engine/function/asset/asset_batch_types.hpp>
#include <gneiss/engine/function/render/camera.hpp>
#include <gneiss/engine/function/render/debug_draw_list.hpp>
#include <gneiss/engine/function/render/statistics.hpp>
#include <gneiss/engine/function/render/ui_draw_list.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace gneiss {
class asset_manager;
namespace files {
class service;
}
namespace render {
class service;
struct frame_image;
}
}

namespace gneiss {}
namespace gneiss {
class world;
}

namespace gneiss {
class resource_load_group;
}

namespace gneiss::runtime {

/** 一个宿主视图；省略相机时选用 World 主相机。目标为零表示窗口。
 * 相机按值保存，目标 RID 借用，调用方须先移除视图再销毁目标。 */
struct application_view {
  rid target{};
  std::optional<view_camera> camera{};
};

/** 内部真实运行对象；仅所属线程操作和释放最后一个持有引用。
 * 私有装配存储位于实现文件；此内部模块接口随源码构建，不属于安装 SDK。 */
class application final {
public:
  /** 返回已初始化对象的拥有引用；失败返回错误并清理部分初始化的资源。
   * 创建、运行及最后释放均限所属线程，不创建对外句柄。 */
  [[nodiscard]] static GNEISS_API gneiss::result<std::shared_ptr<application>>
  create(const application_configuration& config);
  /** 原子替换视图列表；目标不可重复，最多一个窗口视图。
   * 空列表恢复默认主相机窗口视图；全部离屏时自动追加 UI 窗口。
   * 失败保留旧列表；与单视图便捷接口共享同一份状态。 */
  [[nodiscard]] GNEISS_API result<void> set_views(std::span<const application_view> views);
  /** 修改首个视图的相机；空指针恢复 World 主相机。 */
  [[nodiscard]] GNEISS_API gneiss::error set_view_camera(const gneiss::view_camera* camera);
  /** 修改首个视图的借用目标；零表示窗口。调用方负责目标生命周期。 */
  [[nodiscard]] GNEISS_API gneiss::error set_view_target(gneiss::rid target);
  [[nodiscard]] GNEISS_API gneiss::error capture_frame(std::uint32_t width, std::uint32_t height,
                                                       gneiss::render::frame_image& output);
  [[nodiscard]] GNEISS_API gneiss::render::queue_stats render_statistics() const;
  [[nodiscard]] GNEISS_API gneiss::error
  attach_task_executor(gneiss::tasks::task_executor& executor);
  [[nodiscard]] GNEISS_API bool has_asset_loading_service() const noexcept;
  /** 多资源编辑批次；普通单资产直接使用 assets().load<T>()。 */
  [[nodiscard]] GNEISS_API gneiss::result<gneiss::asset_batch_request>
  load_assets(std::span<const gneiss::render_resource_source> sources, bool reload = false);
  /** 所属线程查询共享资产批次是否可提交；场景切换策略由宿主维护。 */
  [[nodiscard]] GNEISS_API bool can_load_assets() const noexcept;

private:
  [[nodiscard]] gneiss::error advance_loading();
  gneiss::resource_load_group* loading_assets() noexcept;
  [[nodiscard]] GNEISS_API gneiss::error advance_resource_io();
  explicit GNEISS_API application(const runtime::application_configuration& config);
  [[nodiscard]] GNEISS_API gneiss::error initialize(std::weak_ptr<application> self);

public:
  GNEISS_API ~application() noexcept;

  application(const application&) = delete;
  application& operator=(const application&) = delete;
  application(application&&) = delete;
  application& operator=(application&&) = delete;

  /** 所属线程、run 之外请求关闭；拒绝新操作，不等待文件和 CPU 任务。
   * 继续调用 advance_shutdown，返回 not_ready 时让出执行权；终态后可销毁宿主执行器。 */
  [[nodiscard]] GNEISS_API gneiss::error request_shutdown();
  /** 非阻塞推进文件/CPU 收尾；最终设备销毁仍使用 Render 的既有关闭流程。 */
  [[nodiscard]] GNEISS_API gneiss::error advance_shutdown();
  [[nodiscard]] GNEISS_API gneiss::error shutdown();
  [[nodiscard]] GNEISS_API gneiss::error run(std::uint64_t max_frame_count);
  [[nodiscard]] GNEISS_API bool is_owner_thread() const noexcept;
  [[nodiscard]] GNEISS_API bool accepts_requests() const noexcept;
  /** 借用宿主 World；普通场景切换保持地址稳定，关闭后禁止解引用。 */
  [[nodiscard]] GNEISS_API gneiss::world* native_world() const noexcept;
  [[nodiscard]] GNEISS_API gneiss::render::service& render() noexcept;
  /** 借用应用级资产管理器；仅所属线程使用，有效期不超过 Application。 */
  [[nodiscard]] GNEISS_API gneiss::asset_manager& assets() noexcept;
  /** 共享宿主文件服务；由 Application 分发和关闭，消费者只提交/取消自己的请求。 */
  [[nodiscard]] GNEISS_API std::shared_ptr<gneiss::files::service> async_files() const noexcept;
  [[nodiscard]] GNEISS_API const gneiss::input::keyboard_state& keyboard_state() const noexcept;
  [[nodiscard]] GNEISS_API const gneiss::input::pointer_state& pointer_state() const noexcept;
  [[nodiscard]] GNEISS_API gneiss::error poll_input(gneiss::input::event& out_event) noexcept;
  [[nodiscard]] GNEISS_API gneiss::error get_window_size(std::uint32_t& out_width,
                                                         std::uint32_t& out_height) const noexcept;
  /** 仅所属线程；成功只表示已接受，主循环在 update 前应用。未启用后端返回 UNSUPPORTED。 */
  [[nodiscard]] GNEISS_API gneiss::error begin_action_map(std::string_view uri);
  /** NOT_READY 表示在途；成功表示已应用。失败保留原映射。 */
  [[nodiscard]] GNEISS_API gneiss::error action_map_status() const noexcept;
  [[nodiscard]] GNEISS_API gneiss::error cancel_action_map() noexcept;
  [[nodiscard]] GNEISS_API gneiss::error
  find_action(std::string_view name, gneiss::input::action_id& out_action) const noexcept;
  [[nodiscard]] GNEISS_API gneiss::error
  get_action_state(gneiss::input::action_id action,
                   gneiss::input::action_state& out_state) const noexcept;
  [[nodiscard]] GNEISS_API bool can_submit_draw_lists() const noexcept;
  [[nodiscard]] GNEISS_API gneiss::error submit_ui_draw_list(const gneiss::ui_draw_view& desc);
  [[nodiscard]] GNEISS_API gneiss::error
  submit_debug_draw_list(std::span<const gneiss::debug_line> lines);
  GNEISS_API void report(gneiss::severity severity, gneiss::category category, gneiss::error result,
                         std::string_view module, std::string_view message);
  GNEISS_API void request_exit() noexcept;
  GNEISS_API void set_paused(bool value) noexcept;

private:
  [[nodiscard]] GNEISS_API std::uint64_t now_ns() const;
  [[nodiscard]] GNEISS_API gneiss::error poll_events(bool& out_should_close);
  [[nodiscard]] GNEISS_API gneiss::error render_frame();

  struct storage;
  std::unique_ptr<storage> storage_;
};

} // namespace gneiss::runtime

#endif
