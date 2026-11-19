// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RUNTIME_APPLICATION_HPP_
#define GNEISS_RUNTIME_APPLICATION_HPP_

#include <gneiss/engine/core/base/result.hpp>
#include <gneiss/engine/export.hpp>

#include <gneiss/engine/function/application/configuration.hpp>
#include <gneiss/engine/function/application/schedule.hpp>

#include <cstdint>
#include <memory>

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
}
namespace input {
class service;
}
}

namespace gneiss::runtime {

/** 内部真实运行对象；仅所属线程操作和释放最后一个持有引用。
 * 私有装配存储位于实现文件；此内部模块接口随源码构建，不属于安装 SDK。 */
class application final {
public:
  /** 创建后、首次 run 前注册；同阶段依注册顺序执行，捕获对象借用至关闭结束。 */
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
  /** 注册一次性宿主启动；服务就绪后、初始状态 Enter 前按注册顺序执行。
   * 无参数，返回 void/result<void>/error。异步业务只在这里提交请求，不阻塞等待。
   * 失败进入停止；on_stop 必须能够清理未启动或部分启动的会话。 */
  template <class F> void on_start(std::string name, F function) {
    static_assert(std::is_invocable_v<F&>);
    register_start_system(std::move(name), app::make_system(std::move(function)));
  }
  /** 首次运行前注册宿主清理；停止时按注册顺序执行，服务仍可借用。
   * 无参数；同步返回 void/result<void>/error，异步返回 stop_progress 或其 result。
   * pending 保留当前步骤供下轮重试；完成或失败不重放，失败不跳过后续清理。 */
  template <class F> void on_stop(std::string name, F function) {
    static_assert(std::is_invocable_v<F&>);
    register_stop_system(std::move(name), app::make_stop_system(std::move(function)));
  }
  /** 创建应用配置与调度所有者；首次 step/run 初始化服务，失败进入统一停止。
   * 创建、运行及最后释放均限所属线程，不创建对外句柄。 */
  [[nodiscard]] static GNEISS_ENGINE_API gneiss::result<std::shared_ptr<application>>
  create(const application_configuration& config);

private:
  [[nodiscard]] GNEISS_ENGINE_API gneiss::error advance_resource_io();
  explicit GNEISS_ENGINE_API application(const runtime::application_configuration& config);
  [[nodiscard]] GNEISS_ENGINE_API gneiss::error initialize(std::weak_ptr<application> self);

public:
  GNEISS_ENGINE_API ~application() noexcept;

  application(const application&) = delete;
  application& operator=(const application&) = delete;
  application(application&&) = delete;
  application& operator=(application&&) = delete;

  /** 驱动完整生命周期至停止；失败也先完成受控关闭。 */
  [[nodiscard]] GNEISS_ENGINE_API result<void> run();
  /** 推进一轮，不等待全部 GPU 帧；stopped 或失败为终态。所属线程，不可重入。 */
  [[nodiscard]] GNEISS_ENGINE_API result<app::lifecycle> step();
  [[nodiscard]] GNEISS_ENGINE_API bool is_owner_thread() const noexcept;
  [[nodiscard]] GNEISS_ENGINE_API bool accepts_requests() const noexcept;
  [[nodiscard]] GNEISS_ENGINE_API gneiss::render::service& render() noexcept;
  /** 借用应用级资产管理器；仅所属线程使用，有效期不超过 Application。 */
  [[nodiscard]] GNEISS_ENGINE_API gneiss::asset_manager& assets() noexcept;
  /** 共享宿主文件服务；启动前为空，由 Application 分发和关闭。 */
  [[nodiscard]] GNEISS_ENGINE_API std::shared_ptr<gneiss::files::service>
  async_files() const noexcept;
  /** 借用输入上下文；限所属线程、服务有效期内使用，不得自行推进平台帧。 */
  [[nodiscard]] GNEISS_ENGINE_API gneiss::input::service& input() noexcept;
  /** 借用只读窗口服务；无窗口或关闭后为空，不允许宿主重复泵送平台事件。 */
  [[nodiscard]] GNEISS_ENGINE_API const gneiss::window::service* window() const noexcept;
  /** 注册唯一窗口关闭请求处理器，首次运行前调用。未注册时直接退出。
   * 已注册时由宿主确认并调用 request_exit；回调捕获须存活至关闭结束。 */
  GNEISS_ENGINE_API void on_close_request(std::function<void()> handler);
  GNEISS_ENGINE_API void request_exit() noexcept;

private:
  [[nodiscard]] GNEISS_ENGINE_API std::uint64_t now_ns() const;
  [[nodiscard]] GNEISS_ENGINE_API gneiss::error poll_events(bool& out_should_close);

  GNEISS_ENGINE_API void register_system(app::stage, std::string, app::system_function);
  GNEISS_ENGINE_API void register_state(app::detail::state_key);
  GNEISS_ENGINE_API void register_state_system(app::detail::state_event, app::detail::state_key,
                                               std::string, app::system_function);
  GNEISS_ENGINE_API result<void> request_state_change(app::detail::state_key);
  GNEISS_ENGINE_API std::uint64_t current_state(type_id) const;
  GNEISS_ENGINE_API void register_start_system(std::string, app::system_function);
  GNEISS_ENGINE_API void register_stop_system(std::string, app::stop_function);
  bool stop_host();
  error shutdown();
  error request_shutdown();
  error advance_shutdown();
  error begin_service_shutdown();
  error advance_frame();
  error configure_executor(tasks::executor&);
  error dispatch_completion_systems();
  struct storage;
  std::unique_ptr<storage> storage_;
};

} // namespace gneiss::runtime

#endif
