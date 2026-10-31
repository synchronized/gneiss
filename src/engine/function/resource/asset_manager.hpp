// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#ifndef GNEISS_ENGINE_ASSET_MANAGER_HPP_
#define GNEISS_ENGINE_ASSET_MANAGER_HPP_

#include "engine/core/result.hpp"
#include "engine/core/type_id.hpp"
#include "engine/function/resource/asset_load_types.hpp"
#include "engine/function/resource/asset_request.hpp"
#include "engine/function/resource/resource_cache.hpp"
#include "engine/resource/asset_decoder.hpp"
#include "engine/resource/asset_identity.hpp"
#include "engine/resource/mesh/mesh_data.hpp"
#include "engine/resource/texture/texture_data.hpp"
#include <memory>
#include <utility>

namespace gneiss::engine {
class task_executor;
struct decoded_asset;
class progress_notification;
struct mesh_view;
struct texture_view;
namespace files {
class service;
}
}

namespace gneiss::engine {
/** 应用级资产管理器。拥有 CPU 数据索引，不拥有 World 或编辑状态。
 * 所属线程使用；请求通道、场景及外部租约须按各自契约释放。
 * 只负责 CPU 数据加载；运行资源创建由 resource_service 负责。 */
class GNEISS_API asset_manager final {
public:
  /** 注入同线程文件服务；宿主驱动文件完成分发，advance 只推进 CPU 解码和资源结果。
   * 不提供同步文件读取；缓存查询使用 find_loaded，服务可由宿主共享持有。
   * 使用默认注册表，宿主须先 initialize_meta。 */
  explicit asset_manager(std::shared_ptr<engine::files::service> files);
  ~asset_manager();
  /** 借用宿主解码执行器；文件 I/O 由服务后端执行，不创建独立线程池。 */
  void attach_task_executor(engine::task_executor& executor);
  /** 宿主每次先分发文件完成，再推进本管理器；不驱动文件或其他服务回调。 */
  void advance();
  /** 请求停止：立即拒绝新加载、终止观察者并取消 I/O/CPU 任务，不等待任务退出。
   * 宿主持有文件服务并继续分发，调用 advance 收尾，直到 stopped。
   * 不自动关闭共享文件服务；已完成资源引用不失效。 */
  void request_stop() noexcept;
  [[nodiscard]] bool stopped() const noexcept;
  /** 迁移期同步关闭兜底：终止观察者并取消文件请求，等待已有 CPU 解码任务收尾。
   * 共享文件服务继续由宿主分发/关闭；独占服务随管理器销毁放弃通知，后端保活实际 I/O。 */
  void stop() noexcept;
  /** 显式注册 CPU 类型；首次请求后冻结注册。来源身份由注入的文件服务确定。 */
  [[nodiscard]] engine::error
  register_decoder(const std::shared_ptr<const asset_decoder>& loader) noexcept;
  /** 无需继承或注册反射；注册失败不改变已有解码器，首次加载后禁止注册；内置 mesh_data/texture_data
   * 使用专用 Loader，不接受覆盖。 */
  template <class T>
  [[nodiscard]] error register_decoder(asset_decode_function<T> function) noexcept {
    if (!function)
      return error::invalid_argument;
    try {
      return register_decoder(std::make_shared<detail::typed_asset_decoder<T>>(function));
    } catch (const std::bad_alloc&) {
      return error::out_of_memory;
    } catch (...) {
      return error::internal;
    }
  }
  /** 默认复用已完成缓存；reload 重新读取并解析，在途同键请求合并。
   * 所属线程调用；执行器须存活至 stop 返回。 */
  template <typename T>
  [[nodiscard]] result<asset_request<T>> load(std::string_view uri,
                                              asset_load_options options) noexcept {
    std::shared_ptr<detail::asset_state> state;
    const auto status =
        request(runtime_type_of<T>(), uri, options, state, &detail::make_reflected_decoder<T>);
    if (status.failed())
      return status;
    return asset_request<T>{std::move(state)};
  }
  template <typename T>
  [[nodiscard]] result<asset_request<T>> load(std::string_view uri, bool reload = false) noexcept {
    asset_load_options options{.reload = reload};
    return load<T>(uri, options);
  }
  /** 只查询已完成缓存，不读取文件或提交任务；未命中返回 not_found。 */
  template <class T>
  [[nodiscard]] result<asset_ref<const T>> find_loaded(std::string_view uri,
                                                       asset_load_options options) const noexcept {
    std::shared_ptr<const void> value;
    const auto status = find_loaded_erased(runtime_type_of<T>(), uri, value, options);
    if (status.failed())
      return status;
    return asset_ref<const T>{std::static_pointer_cast<const T>(value)};
  }
  template <class T>
  [[nodiscard]] result<asset_ref<const T>> find_loaded(std::string_view uri) const noexcept {
    asset_load_options options;
    return find_loaded<T>(uri, options);
  }
  /** 所属线程采用已经构造的数据；成功后较早异步请求不能覆盖缓存。 */
  template <class T>
  [[nodiscard]] result<asset_ref<const T>>
  store(std::string_view uri, std::shared_ptr<const T> value, asset_load_options options) noexcept {
    std::shared_ptr<const void> stored;
    const auto status = store_erased(runtime_type_of<T>(), uri, std::move(value), stored, options);
    if (status.failed())
      return status;
    return asset_ref<const T>{std::static_pointer_cast<const T>(stored)};
  }
  template <class T>
  [[nodiscard]] result<asset_ref<const T>> store(std::string_view uri,
                                                 std::shared_ptr<const T> value) noexcept {
    asset_load_options options;
    return store<T>(uri, std::move(value), options);
  }
  asset_manager(const asset_manager&) = delete;
  asset_manager& operator=(const asset_manager&) = delete;
  /** 借用同线程异步文件服务；停止后为空。 */
  [[nodiscard]] std::shared_ptr<engine::files::service> async_files() const noexcept;
  /** 解析统一来源身份，不执行文件 I/O。 */
  [[nodiscard]] result<asset_identity> identify(std::string_view uri,
                                                std::uint32_t type) const noexcept;
  /** 所属线程回收无外部引用且无在途任务的缓存；不强制卸载活跃资源。 */
  void collect_unused() noexcept;
  /** 内部加载流程借用唯一资源索引；所属线程使用，不转移缓存所有权。 */
  [[nodiscard]] resource_cache& cache() noexcept { return cache_; }

private:
  using decoder_factory = result<std::shared_ptr<const asset_decoder>> (*)();
  [[nodiscard]] error resolve_decoder(type_id, decoder_factory,
                                      std::shared_ptr<const asset_decoder>&);
  [[nodiscard]] engine::error request(type_id type, std::string_view uri,
                                      asset_load_options options,
                                      std::shared_ptr<detail::asset_state>& output,
                                      decoder_factory fallback) noexcept;
  error find_loaded_erased(type_id, std::string_view, std::shared_ptr<const void>&,
                           asset_load_options) const noexcept;
  error store_erased(type_id, std::string_view, std::shared_ptr<const void>,
                     std::shared_ptr<const void>&, asset_load_options) noexcept;
  struct resource_state;
  std::unique_ptr<resource_state> resource_state_;
  resource_cache cache_;
};
}

#endif
