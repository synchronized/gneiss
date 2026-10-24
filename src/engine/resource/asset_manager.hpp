// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#ifndef GNEISS_ENGINE_ASSET_MANAGER_HPP_
#define GNEISS_ENGINE_ASSET_MANAGER_HPP_

#include "asset_request.hpp"
#include "engine/core/result.hpp"
#include "engine/resource/cache/resource_cache.hpp"
#include "engine/resource/loaders/prefab_asset_loader.hpp"
#include "engine/resource/types/action_map.hpp"
#include "engine/resource/vfs/virtual_file_system.hpp"
#include "loaders/asset_loader.hpp"
#include "types/action_map_asset.hpp"
#include <memory>
#include <utility>

namespace gneiss::engine {
class task_executor;
}

namespace gneiss::engine {
/** 应用级资产管理器。拥有统一资源索引与 Prefab 加载器，不拥有 World 或编辑状态。
 * 所属线程使用；请求通道、场景及外部租约须按各自契约释放。
 * 功能层装配类型加载器；底层 asset 模块不依赖 Scene/Render。 */
class GNEISS_API asset_manager final {
public:
  explicit asset_manager(gneiss::engine::virtual_file_system files);
  ~asset_manager();
  /** 借用 VFS 与宿主执行器；不创建独立线程池。 */
  void attach_task_executor(gneiss::engine::task_executor& executor);
  /** 同步准备失败不改输出，不驱动宿主任务队列；采用与取消由调用者负责。 */
  [[nodiscard]] gneiss::engine::result load_input(std::string_view uri,
                                                  gneiss::engine::action_map& output) noexcept;
  [[nodiscard]] gneiss::engine::result
  request_input(std::string_view uri, asset_request<action_map_asset>& output) noexcept;
  void advance_resources();
  /** 调用者先取消并释放请求，再停止资源工作。 */
  void stop_resources() noexcept;
  /** 显式注册 CPU 类型；首次请求后冻结注册。VFS 来源由本管理器固定持有。 */
  [[nodiscard]] gneiss::engine::result
  register_loader(const std::shared_ptr<const asset_loader>& loader) noexcept;
  /** 默认复用已完成缓存；reload 重新读取并解析，在途同键请求合并。
   * 所属线程调用；执行器须存活至 stop_resources 返回。 */
  template <typename T>
  [[nodiscard]] gneiss::engine::result load(std::string_view uri, asset_request<T>& output,
                                            bool reload = false) noexcept {
    std::shared_ptr<detail::asset_state> state;
    const auto status = request(typeid(T), uri, reload, state);
    if (status == gneiss::engine::result::success)
      output = asset_request<T>{std::move(state)};
    return status;
  }
  /** 同步读取不驱动任务队列；成功发布使同键较早异步请求失去当前缓存写入权。 */
  template <typename T>
  [[nodiscard]] gneiss::engine::result load_sync(std::string_view uri,
                                                 asset_ref<const T>& output) noexcept {
    std::shared_ptr<const void> value;
    const auto status = load_sync_erased(typeid(T), uri, value);
    if (status == gneiss::engine::result::success)
      output = asset_ref<const T>{std::static_pointer_cast<const T>(value)};
    return status;
  }
  asset_manager(const asset_manager&) = delete;
  asset_manager& operator=(const asset_manager&) = delete;
  [[nodiscard]] const gneiss::engine::virtual_file_system& files() const noexcept { return files_; }
  /** 所属线程回收无外部引用且无在途任务的缓存；不强制卸载活跃资源。 */
  void collect_unused() noexcept;
  [[nodiscard]] gneiss::engine::result
  acquire(std::string_view uri, gneiss::engine::prefab_asset_ref& output,
          gneiss::engine::scene_diagnostic& diagnostic) noexcept {
    return prefabs_.acquire(uri, output, diagnostic);
  }
  /** 功能层 Loader 借用唯一资源索引；所属线程使用，不转移缓存所有权。 */
  [[nodiscard]] resource_cache& cache() noexcept { return cache_; }
  [[nodiscard]] gneiss::engine::prefab_asset_loader& prefab_loader() noexcept { return prefabs_; }

private:
  [[nodiscard]] gneiss::engine::result
  request(std::type_index type, std::string_view uri, bool reload,
          std::shared_ptr<detail::asset_state>& output) noexcept;
  [[nodiscard]] gneiss::engine::result
  load_sync_erased(std::type_index type, std::string_view uri,
                   std::shared_ptr<const void>& output) noexcept;
  struct resource_state;
  std::unique_ptr<resource_state> resource_state_;
  gneiss::engine::virtual_file_system files_;
  gneiss::engine::resource_cache cache_;
  gneiss::engine::prefab_asset_loader prefabs_;
};
}

#endif
