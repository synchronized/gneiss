// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#ifndef GNEISS_ENGINE_ASSET_MANAGER_HPP_
#define GNEISS_ENGINE_ASSET_MANAGER_HPP_

#include "action_map_request.hpp"
#include "engine/resource/cache/cpu_document_cache.hpp"
#include "engine/resource/cache/resource_cache.hpp"
#include "engine/resource/loaders/prefab_asset_loader.hpp"
#include "engine/resource/types/action_map.hpp"
#include "engine/resource/vfs/virtual_file_system.hpp"
#include <memory>
#include <utility>

namespace gneiss::engine::core::tasks {
class task_executor;
}

namespace gneiss::engine::resource {
/** 应用级资产管理器。拥有 CPU 文档缓存与 Prefab 加载器，不拥有 World 或编辑状态。
 * 所属线程使用；请求通道、场景及外部租约须按各自契约释放。
 * 功能层装配类型加载器；底层 asset 模块不依赖 Scene/Render。 */
class asset_manager final {
public:
  explicit asset_manager(engine::resource::virtual_file_system files);
  ~asset_manager();
  /** 借用 VFS 与宿主执行器；不创建独立线程池。 */
  void attach_task_executor(engine::core::tasks::task_executor& executor);
  /** 同步准备失败不改输出，不驱动宿主任务队列；采用与取消由调用者负责。 */
  [[nodiscard]] result load_input(std::string_view uri,
                                  engine::resource::action_map& output) noexcept;
  [[nodiscard]] result request_input(std::string_view uri,
                                     std::unique_ptr<action_map_request>& output) noexcept;
  void advance_resources();
  /** 调用者先取消并释放请求，再停止资源工作。 */
  void stop_resources() noexcept;
  asset_manager(const asset_manager&) = delete;
  asset_manager& operator=(const asset_manager&) = delete;
  [[nodiscard]] const engine::resource::virtual_file_system& files() const noexcept {
    return files_;
  }
  void collect_unused() noexcept {
    prefab_cache_.release_unused();
    if (documents_) {
      documents_->collect();
    }
  }
  [[nodiscard]] gneiss_result acquire(std::string_view uri,
                                      engine::resource::prefab_asset_lease& output,
                                      engine::resource::scene_diagnostic& diagnostic) noexcept {
    return prefabs_.acquire(uri, output, diagnostic);
  }
  /** 共享 CPU 文档缓存的所有权以装配请求服务；不提供上传或设备配置。 */
  [[nodiscard]] std::shared_ptr<engine::resource::cpu_document_cache> documents() const noexcept {
    return documents_;
  }
  [[nodiscard]] engine::resource::prefab_asset_loader& prefab_loader() noexcept { return prefabs_; }

private:
  struct resource_state;
  std::unique_ptr<resource_state> resource_state_;
  engine::resource::virtual_file_system files_;
  engine::resource::resource_cache prefab_cache_;
  std::shared_ptr<engine::resource::cpu_document_cache> documents_{
      engine::resource::make_cpu_document_cache()};
  engine::resource::prefab_asset_loader prefabs_;
};
}

#endif
