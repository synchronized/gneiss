// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RUNTIME_APPLICATION_HPP_
#define GNEISS_RUNTIME_APPLICATION_HPP_

#include <gneiss/engine/core/base/result.hpp>
#include <gneiss/engine/core/base/rid.hpp>
#include <gneiss/engine/export.hpp>
#include <gneiss/engine/function/asset/asset_manager.hpp>
#include <gneiss/engine/function/input/action.hpp>
#include <gneiss/engine/function/scene/scene_types.hpp>
#include <gneiss/engine/function/scene/world.hpp>
#include <gneiss/engine/platform/window/input.hpp>
#include <gneiss/engine/resource/input/action_map.hpp>

#include <gneiss/engine/core/logging/record.hpp>
#include <gneiss/engine/function/application/configuration.hpp>
#include <gneiss/engine/function/application/schedule.hpp>
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
namespace window {
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

/** 内部真实运行对象；仅所属线程操作和释放最后一个持有引用。
 * 私有装配存储位于实现文件；此内部模块接口随源码构建，不属于安装 SDK。 */
class application final {
public:
  /** 创建后、首次 run 前注册；同阶段依注册顺序执行，捕获对象借用至关闭结束。
   * update 系统与旧 callbacks.update 不得混用。 */
  template <class F> void add_system(app::stage phase, std::string name, F function) {
    register_system(phase, std::move(name), app::make_system(std::move(function)));
  }
  /** 注册业务枚举的初始状态；首次运行前调用，每种类型只能注册一次。 */
  template <class T>
    requires std::is_enum_v<T>
  void init_state(T initial) {
    register_state(app::detail::key(initial));
  }
  /** 请求下一边界转换；同目标合并，不同目标返回 invalid_state 并保留首次请求。 */
  template <class T>
    requires std::is_enum_v<T>
  result<void> request_state(T target) {
    return request_state_change(app::detail::key(target));
  }
  /** 所属线程查询已注册状态；首次 Enter 前返回配置的初始值。 */
  template <class T>
    requires std::is_enum_v<T>
  T state() const {
    return static_cast<T>(current_state(runtime_type_of<T>()));
  }
  template <class T, class F>
    requires std::is_enum_v<T>
  void on_enter(T state, std::string name, F function) {
    register_state_system(app::detail::state_event::enter, app::detail::key(state), std::move(name),
                          app::make_system(std::move(function)));
  }
  template <class T, class F>
    requires std::is_enum_v<T>
  void on_update(T state, std::string name, F function) {
    register_state_system(app::detail::state_event::update, app::detail::key(state),
                          std::move(name), app::make_system(std::move(function)));
  }
  template <class T, class F>
    requires std::is_enum_v<T>
  void on_exit(T state, std::string name, F function) {
    register_state_system(app::detail::state_event::exit, app::detail::key(state), std::move(name),
                          app::make_system(std::move(function)));
  }
  /** 首次运行前注册宿主清理；停止时按注册顺序执行，服务仍可借用。
   * 无参数；同步返回 void/result<void>/error，异步返回 stop_progress 或其 result。
   * pending 保留当前步骤供下轮重试；完成或失败不重放，失败不跳过后续清理。 */
  template <class F> void on_stop(std::string name, F function) {
    static_assert(std::is_invocable_v<F&>);
    register_stop_system(std::move(name), app::make_stop_system(std::move(function)));
  }
  /** 返回已初始化对象的拥有引用；失败返回错误并清理部分初始化的资源。
   * 创建、运行及最后释放均限所属线程，不创建对外句柄。 */
  [[nodiscard]] static GNEISS_ENGINE_API gneiss::result<std::shared_ptr<application>>
  create(const application_configuration& config);
  [[nodiscard]] GNEISS_ENGINE_API gneiss::render::queue_stats render_statistics() const;
  [[nodiscard]] GNEISS_ENGINE_API bool has_asset_loading_service() const noexcept;
  /** 多资源编辑批次；普通单资产直接使用 assets().load<T>()。 */
  [[nodiscard]] GNEISS_ENGINE_API gneiss::result<gneiss::asset_batch_request>
  load_assets(std::span<const gneiss::render_resource_source> sources, bool reload = false);
  /** 所属线程查询共享资产批次是否可提交；场景切换策略由宿主维护。 */
  [[nodiscard]] GNEISS_ENGINE_API bool can_load_assets() const noexcept;

private:
  [[nodiscard]] gneiss::error advance_loading();
  gneiss::resource_load_group* loading_assets() noexcept;
  [[nodiscard]] GNEISS_ENGINE_API gneiss::error advance_resource_io();
  explicit GNEISS_ENGINE_API application(const runtime::application_configuration& config);
  [[nodiscard]] GNEISS_ENGINE_API gneiss::error initialize(std::weak_ptr<application> self);

public:
  GNEISS_ENGINE_API ~application() noexcept;

  application(const application&) = delete;
  application& operator=(const application&) = delete;
  application(application&&) = delete;
  application& operator=(application&&) = delete;

  [[nodiscard]] GNEISS_ENGINE_API gneiss::error shutdown();
  /** 驱动完整生命周期至停止；失败也先完成受控关闭。 */
  [[nodiscard]] GNEISS_ENGINE_API result<void> run();
  /** 推进一轮，不等待全部 GPU 帧；stopped 或失败为终态。所属线程，不可重入。 */
  [[nodiscard]] GNEISS_ENGINE_API result<app::lifecycle> step();
  /** 迁移期间的有界旧入口，不自动关闭；新宿主使用 run()/step()。 */
  [[nodiscard]] GNEISS_ENGINE_API gneiss::error run(std::uint64_t max_frame_count);
  [[nodiscard]] GNEISS_ENGINE_API bool is_owner_thread() const noexcept;
  [[nodiscard]] GNEISS_ENGINE_API bool accepts_requests() const noexcept;
  [[nodiscard]] GNEISS_ENGINE_API gneiss::render::service& render() noexcept;
  /** 借用应用级资产管理器；仅所属线程使用，有效期不超过 Application。 */
  [[nodiscard]] GNEISS_ENGINE_API gneiss::asset_manager& assets() noexcept;
  /** 共享宿主文件服务；由 Application 分发和关闭，消费者只提交/取消自己的请求。 */
  [[nodiscard]] GNEISS_ENGINE_API std::shared_ptr<gneiss::files::service>
  async_files() const noexcept;
  [[nodiscard]] GNEISS_ENGINE_API const gneiss::input::keyboard_state&
  keyboard_state() const noexcept;
  [[nodiscard]] GNEISS_ENGINE_API const gneiss::input::pointer_state&
  pointer_state() const noexcept;
  [[nodiscard]] GNEISS_ENGINE_API gneiss::error
  poll_input(gneiss::input::event& out_event) noexcept;
  /** 借用只读窗口服务；无窗口或关闭后为空，不允许宿主重复泵送平台事件。 */
  [[nodiscard]] GNEISS_ENGINE_API const gneiss::window::service* window() const noexcept;
  /** 仅所属线程；成功只表示已接受，主循环在 update 前应用。未启用后端返回 UNSUPPORTED。 */
  [[nodiscard]] GNEISS_ENGINE_API gneiss::error begin_action_map(std::string_view uri);
  /** NOT_READY 表示在途；成功表示已应用。失败保留原映射。 */
  [[nodiscard]] GNEISS_ENGINE_API gneiss::error action_map_status() const noexcept;
  [[nodiscard]] GNEISS_ENGINE_API gneiss::error cancel_action_map() noexcept;
  [[nodiscard]] GNEISS_ENGINE_API gneiss::error
  find_action(std::string_view name, gneiss::input::action_id& out_action) const noexcept;
  [[nodiscard]] GNEISS_ENGINE_API gneiss::error
  get_action_state(gneiss::input::action_id action,
                   gneiss::input::action_state& out_state) const noexcept;
  GNEISS_ENGINE_API void report(gneiss::severity severity, gneiss::category category,
                                gneiss::error result, std::string_view module,
                                std::string_view message);
  GNEISS_ENGINE_API void request_exit() noexcept;
  GNEISS_ENGINE_API void set_paused(bool value) noexcept;

private:
  [[nodiscard]] GNEISS_ENGINE_API std::uint64_t now_ns() const;
  [[nodiscard]] GNEISS_ENGINE_API gneiss::error poll_events(bool& out_should_close);

  GNEISS_ENGINE_API void register_system(app::stage, std::string, app::system_function);
  GNEISS_ENGINE_API void register_state(app::detail::state_key);
  GNEISS_ENGINE_API void register_state_system(app::detail::state_event, app::detail::state_key,
                                               std::string, app::system_function);
  GNEISS_ENGINE_API result<void> request_state_change(app::detail::state_key);
  GNEISS_ENGINE_API std::uint64_t current_state(type_id) const;
  GNEISS_ENGINE_API void register_stop_system(std::string, app::stop_function);
  bool stop_host();
  error request_shutdown();
  error advance_shutdown();
  error begin_service_shutdown();
  error advance_frame();
  error configure_executor(tasks::task_executor&);
  error dispatch_completion_systems();
  struct storage;
  std::unique_ptr<storage> storage_;
};

} // namespace gneiss::runtime

#endif
