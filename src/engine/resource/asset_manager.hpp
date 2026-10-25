// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#ifndef GNEISS_ENGINE_ASSET_MANAGER_HPP_
#define GNEISS_ENGINE_ASSET_MANAGER_HPP_

#include "asset_request.hpp"
#include "engine/core/result.hpp"
#include "engine/core/type_id.hpp"
#include "engine/resource/cache/resource_cache.hpp"
#include "engine/resource/types/action_map.hpp"
#include "engine/resource/vfs/virtual_file_system.hpp"
#include "loaders/asset_decoder.hpp"
#include <memory>
#include <utility>

namespace gneiss::engine {
class task_executor;
class file_service;
}

namespace gneiss::engine {
/** 应用级资产管理器。拥有统一资源索引，不拥有 World 或编辑状态。
 * 所属线程使用；请求通道、场景及外部租约须按各自契约释放。
 * 功能层装配类型加载器；底层 asset 模块不依赖 Scene/Render。 */
class GNEISS_API asset_manager final {
public:
  /** 默认 Registry 包含内置资源 codec；传入的 Registry 必须已冻结。
   * 管理器持有 Registry；fallback_limit 限制反射资源输入，显式 decoder 可指定自己的上限。 */
  explicit asset_manager(gneiss::engine::virtual_file_system files, registry_ref registry = {},
                         std::size_t fallback_limit = 64U * 1024U * 1024U);
  /** 注入同线程文件服务；advance_resources 驱动完成分发，只提交 CPU 解码任务。
   * 此入口不提供同步文件读取，load_sync 仅能复用已有缓存；服务可由宿主共享持有。 */
  explicit asset_manager(std::shared_ptr<file_service> files, registry_ref registry = {},
                         std::size_t fallback_limit = 64U * 1024U * 1024U);
  ~asset_manager();
  /** 借用宿主解码执行器；文件 I/O 由服务后端执行，不创建独立线程池。 */
  void attach_task_executor(gneiss::engine::task_executor& executor);
  /** 同步准备失败不改输出，不驱动宿主任务队列；采用与取消由调用者负责。 */
  [[nodiscard]] gneiss::engine::result load_input(std::string_view uri,
                                                  gneiss::engine::action_map& output) noexcept;
  [[nodiscard]] gneiss::engine::result request_input(std::string_view uri,
                                                     asset_request<action_map>& output) noexcept;
  void advance_resources();
  /** 终止资产观察者并取消文件请求，等待已有 CPU 解码任务收尾。
   * 共享文件服务继续由宿主分发/关闭；独占服务随管理器销毁放弃通知，后端保活实际 I/O。 */
  void stop_resources() noexcept;
  /** 显式注册 CPU 类型；首次请求后冻结注册。VFS 来源由本管理器固定持有。 */
  [[nodiscard]] gneiss::engine::result
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
  [[nodiscard]] gneiss::engine::result load(std::string_view uri, asset_request<T>& output,
                                            bool reload = false) noexcept {
    std::shared_ptr<detail::asset_state> state;
    const auto status = request(gneiss::engine::runtime_type_of<T>(), uri, reload, state,
                                &detail::make_reflected_decoder<T>);
    if (status == gneiss::engine::result::success)
      output = asset_request<T>{std::move(state)};
    return status;
  }
  /** 同步加载默认复用缓存；仅旧 VFS 构造入口支持缓存未命中时同步读文件。
   * 注入文件服务时未命中返回 unsupported；成功读取使较早异步请求失去缓存写入权。 */
  template <typename T>
  [[nodiscard]] gneiss::engine::result
  load_sync(std::string_view uri, asset_ref<const T>& output, bool reload = false,
            serialization_diagnostic* diagnostic = nullptr) noexcept {
    std::shared_ptr<const void> value;
    const auto status = load_sync_erased(gneiss::engine::runtime_type_of<T>(), uri, value,
                                         &detail::make_reflected_decoder<T>, reload, diagnostic);
    if (status == gneiss::engine::result::success)
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
  [[nodiscard]] const gneiss::engine::virtual_file_system& files() const noexcept { return files_; }
  /** 借用同线程异步文件服务；停止后为空。 */
  [[nodiscard]] std::shared_ptr<file_service> async_files() const noexcept;
  /** 解析统一来源身份，不执行文件 I/O。 */
  [[nodiscard]] result identify(std::string_view uri, std::uint32_t type,
                                asset_identity& output) const noexcept;
  /** 所属线程回收无外部引用且无在途任务的缓存；不强制卸载活跃资源。 */
  void collect_unused() noexcept;
  /** 功能层 Loader 借用唯一资源索引；所属线程使用，不转移缓存所有权。 */
  [[nodiscard]] resource_cache& cache() noexcept { return cache_; }

private:
  using decoder_factory = result (*)(const registry_ref&, std::size_t,
                                     std::shared_ptr<const asset_decoder>&);
  [[nodiscard]] result resolve_decoder(type_id, decoder_factory,
                                       std::shared_ptr<const asset_decoder>&);
  [[nodiscard]] gneiss::engine::result request(gneiss::engine::type_id type, std::string_view uri,
                                               bool reload,
                                               std::shared_ptr<detail::asset_state>& output,
                                               decoder_factory fallback) noexcept;
  [[nodiscard]] gneiss::engine::result
  load_sync_erased(gneiss::engine::type_id type, std::string_view uri,
                   std::shared_ptr<const void>& output, decoder_factory fallback, bool reload,
                   serialization_diagnostic* diagnostic) noexcept;
  result store_erased(type_id, std::string_view, std::shared_ptr<const void>,
                      std::shared_ptr<const void>&) noexcept;
  struct resource_state;
  std::unique_ptr<resource_state> resource_state_;
  gneiss::engine::virtual_file_system files_;
  gneiss::engine::resource_cache cache_;
};
}

#endif
