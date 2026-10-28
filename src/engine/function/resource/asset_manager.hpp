// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#ifndef GNEISS_ENGINE_ASSET_MANAGER_HPP_
#define GNEISS_ENGINE_ASSET_MANAGER_HPP_

#include "engine/core/result.hpp"
#include "engine/core/type_id.hpp"
#include "engine/function/resource/asset_load_types.hpp"
#include "engine/resource/asset_decoder.hpp"
#include "engine/resource/asset_request.hpp"
#include "engine/resource/input/action_map.hpp"
#include "engine/function/resource/resource_cache.hpp"
#include <memory>
#include <utility>

namespace gneiss::engine {
class task_executor;
class render_service;
class progress_notification;
struct mesh_view;
struct texture_view;
namespace files {
class service;
}
}

namespace gneiss::engine {
/** 应用级资产管理器。拥有统一资源索引，不拥有 World 或编辑状态。
 * 所属线程使用；请求通道、场景及外部租约须按各自契约释放。
 * 功能层统一拥有加载编排与资源包装；底层资源值不依赖本管理器。 */
class GNEISS_API asset_manager final {
public:
  /** 注入同线程文件服务；宿主驱动文件完成分发，advance_resources 只推进 CPU 解码和资源结果。
   * 不提供同步文件读取；缓存查询使用 find_loaded，服务可由宿主共享持有。 */
  explicit asset_manager(std::shared_ptr<engine::files::service> files,
                         meta::registry_ref registry = {},
                         std::size_t fallback_limit = 64U * 1024U * 1024U);
  ~asset_manager();
  /** 借用宿主解码执行器；文件 I/O 由服务后端执行，不创建独立线程池。 */
  void attach_task_executor(engine::task_executor& executor);
  [[nodiscard]] engine::result request_input(std::string_view uri,
                                             asset_request<action_map>& output) noexcept;
  /** 宿主每次先分发文件完成，再推进本管理器；不驱动文件或其他服务回调。 */
  void advance_resources();
  /** 请求停止：立即拒绝新加载、终止观察者并取消 I/O/CPU 任务，不等待任务退出。
   * 宿主持有文件服务并继续分发，调用 advance_resources 收尾，直到 resources_stopped。
   * 不自动关闭共享文件服务；已完成资源引用不失效。 */
  void request_stop_resources() noexcept;
  [[nodiscard]] bool resources_stopped() const noexcept;
  /** 迁移期同步关闭兜底：终止观察者并取消文件请求，等待已有 CPU 解码任务收尾。
   * 共享文件服务继续由宿主分发/关闭；独占服务随管理器销毁放弃通知，后端保活实际 I/O。 */
  void stop_resources() noexcept;
  /** 显式注册 CPU 类型；首次请求后冻结注册。来源身份由注入的文件服务确定。 */
  [[nodiscard]] engine::result
  register_decoder(const std::shared_ptr<const asset_decoder>& loader) noexcept;
  /** 无需继承或注册反射；注册失败不改变已有解码器，首次加载后禁止注册。 */
  template <class T>
  [[nodiscard]] result register_decoder(asset_decode_function<T> function,
                                        std::size_t maximum_bytes) noexcept {
    if (!function || maximum_bytes == 0)
      return result::invalid_argument;
    try {
      return register_decoder(
          std::make_shared<detail::typed_asset_decoder<T>>(function, maximum_bytes));
    } catch (const std::bad_alloc&) {
      return result::out_of_memory;
    } catch (...) {
      return result::internal;
    }
  }
  /** 默认复用已完成缓存；reload 重新读取并解析，在途同键请求合并。
   * 所属线程调用；执行器须存活至 stop_resources 返回。 */
  template <typename T>
  [[nodiscard]] engine::result load(std::string_view uri, asset_request<T>& output,
                                    bool reload = false) noexcept {
    std::shared_ptr<detail::asset_state> state;
    const auto status = request(engine::runtime_type_of<T>(), uri, reload, state,
                                &detail::make_reflected_decoder<T>);
    if (status == engine::result::success)
      output = asset_request<T>{std::move(state)};
    return status;
  }
  /** 只查询已完成缓存，不读取文件或提交任务；未命中返回 not_found，失败保持输出。 */
  template <class T>
  [[nodiscard]] result find_loaded(std::string_view uri,
                                   asset_ref<const T>& output) const noexcept {
    std::shared_ptr<const void> value;
    const auto status = find_loaded_erased(runtime_type_of<T>(), uri, value);
    if (status.ok())
      output = asset_ref<const T>{std::static_pointer_cast<const T>(value)};
    return status;
  }
  /** 所属线程采用已经构造的数据；成功后较早异步请求不能覆盖缓存。 */
  template <class T>
  [[nodiscard]] result store(std::string_view uri, std::shared_ptr<const T> value,
                             asset_ref<const T>& output) noexcept {
    std::shared_ptr<const void> stored;
    const auto status = store_erased(runtime_type_of<T>(), uri, std::move(value), stored);
    if (status == result::success)
      output = asset_ref<const T>{std::static_pointer_cast<const T>(stored)};
    return status;
  }
  asset_manager(const asset_manager&) = delete;
  asset_manager& operator=(const asset_manager&) = delete;
  /** 借用同线程异步文件服务；停止后为空。 */
  [[nodiscard]] std::shared_ptr<engine::files::service> async_files() const noexcept;
  /** 解析统一来源身份，不执行文件 I/O。 */
  [[nodiscard]] result identify(std::string_view uri, std::uint32_t type,
                                asset_identity& output) const noexcept;
  /** 所属线程回收无外部引用且无在途任务的缓存；不强制卸载活跃资源。 */
  void collect_unused() noexcept;
  /** 功能层 Loader 借用唯一资源索引；所属线程使用，不转移缓存所有权。 */
  [[nodiscard]] resource_cache& cache() noexcept { return cache_; }

  /** 绑定渲染服务；必须覆盖管理器寿命。纯 CPU 使用者无需调用。 */
  result attach_render(render_service& render, texture_prepare_profile profile = {});
  /** 装配批次加载器；首次配置后保留执行器与 profile，停止后拒绝重新装配。 */
  result configure_loading(task_executor& executor, texture_prepare_profile profile = {},
                           std::shared_ptr<progress_notification> notification = {});
  using asset_update = render_asset_update;
  static constexpr std::size_t maximum_prepared_bytes = 256U * 1024U * 1024U;
  result reserve_asset(const render_asset_reload&, render_asset_ref&) noexcept;
  result acquire_cached(const render_asset_reload&, render_asset_ref&,
                        texture_prepare_profile = {}) const noexcept;
  result acquire_asset(render_asset_type, std::string_view, render_asset_ref&,
                       asset_diagnostic&) noexcept;
  result acquire_mesh(std::string_view, mesh&, asset_diagnostic&) noexcept;
  result acquire_texture(std::string_view, texture&, asset_diagnostic&) noexcept;
  result acquire_material(std::string_view, material&, asset_diagnostic&) noexcept;
  static mesh mesh_ref(const render_asset_ref& value) noexcept {
    return asset_cast<mesh_resource>(value);
  }
  static material material_ref(const render_asset_ref& value) noexcept {
    return asset_cast<material_resource>(value);
  }
  bool owns(const mesh&) const noexcept;
  bool owns(const texture&) const noexcept;
  bool owns(const material&) const noexcept;
  bool owns(const render_asset_ref&) const noexcept;
  /** 所属线程创建程序化资源并返回强引用；失败清空输出，不接管外部 RID。 */
  result create_texture(const texture_view&, texture&) noexcept;
  result create_mesh(const mesh_view&, mesh&) noexcept;
  result create_material(const material_parameters&, std::array<texture, 5>, material&) noexcept;
  result reserve_texture(texture&) noexcept;
  result reserve_mesh(mesh&) noexcept;
  result reserve_material(material&) noexcept;
  /** 仅锁定本管理器已创建的对象；未知或已释放 RID 返回空。 */
  texture find_texture(rid) const noexcept;
  mesh find_mesh(rid) const noexcept;
  material find_material(rid) const noexcept;
  /** 同一时刻接受一个批次；take 领取 CPU 内容采用结果，不等待 GPU。
   * 输入在提交时复制；执行器和 Render 必须覆盖管理器的运行与关闭阶段。 */
  result submit_assets(std::span<const render_asset_reload> sources, std::uint64_t session,
                       std::uint64_t revision, std::uint64_t& request, bool reload = true,
                       std::size_t limit = maximum_prepared_bytes);
  result submit(std::span<const std::string>, std::uint64_t, std::uint64_t, std::uint64_t&,
                bool reload = true);
  bool progress(asset_load_progress&) const;
  bool take(texture_load_completion&);
  bool busy() const noexcept;
  bool cancel();
  bool cancel_unpublished();
  texture_prepare_profile profile() const noexcept;
  void set_default_profile(texture_prepare_profile profile) noexcept;
  std::span<const render_asset_ref> requested_assets() const;
  void advance();
  /** 仅停止视觉资源批次；完整关闭使用 request_stop_resources。 */
  void request_stop();
  bool stopped() const;
  void release_unused() noexcept;
  std::uint64_t next_update_sequence() noexcept;
  bool claim_update(const render_asset_ref&, std::uint64_t) noexcept;
  result prepare_update(prepared_render_asset, asset_update&, const render_asset_ref& = {},
                        std::uint64_t = 0) noexcept;
  result apply_update(asset_update&) noexcept;

private:
  bool render_stopping_{};
  struct render_state;
  std::unique_ptr<render_state> render_state_;
  void advance_render();
  void stop_render() noexcept;
  using decoder_factory = result (*)(const meta::registry_ref&, std::size_t,
                                     std::shared_ptr<const asset_decoder>&);
  [[nodiscard]] result resolve_decoder(type_id, decoder_factory,
                                       std::shared_ptr<const asset_decoder>&);
  [[nodiscard]] engine::result request(type_id type, std::string_view uri, bool reload,
                                       std::shared_ptr<detail::asset_state>& output,
                                       decoder_factory fallback) noexcept;
  result find_loaded_erased(type_id, std::string_view, std::shared_ptr<const void>&) const noexcept;
  result store_erased(type_id, std::string_view, std::shared_ptr<const void>,
                      std::shared_ptr<const void>&) noexcept;
  struct resource_state;
  std::unique_ptr<resource_state> resource_state_;
  resource_cache cache_;
};
}

#endif
