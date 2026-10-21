// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#ifndef GNEISS_ENGINE_ASSET_MANAGER_HPP_
#define GNEISS_ENGINE_ASSET_MANAGER_HPP_

#include "action_map_request.hpp"
#include "engine/asset/action_map.hpp"
#include "engine/asset/cpu_document_cache.hpp"
#include "engine/asset/resource_cache.hpp"
#include "engine/asset/virtual_file_system.hpp"
#include "engine/function/render/render_asset_loader.hpp"
#include "engine/function/scene/prefab_asset_loader.hpp"
#include <memory>
#include <utility>

namespace gneiss::tasks {
class task_executor;
}

namespace gneiss::engine {
/** 应用级资产管理器。唯一拥有共享缓存和类型加载器，不拥有 World 或编辑状态。
 * 所属线程使用；请求通道、场景及外部租约须按各自契约释放，渲染资源服务最后销毁。
 * 功能层装配类型加载器；底层 asset 模块不依赖 Scene/Render。 */
class asset_manager final {
public:
  asset_manager(asset_internal::virtual_file_system files,
                render_internal::render_resource_service& resources);
  ~asset_manager();
  /** 借用 VFS 与宿主执行器；不创建独立线程池。 */
  void attach_task_executor(tasks::task_executor& executor);
  /** 同步准备失败不改输出，不驱动宿主任务队列；采用与取消由调用者负责。 */
  [[nodiscard]] result load_input(std::string_view uri,
                                  asset_internal::action_map& output) noexcept;
  [[nodiscard]] result request_input(std::string_view uri,
                                     std::unique_ptr<action_map_request>& output) noexcept;
  void advance_resources();
  /** 调用者先取消并释放请求，再停止资源工作。 */
  void stop_resources() noexcept;
  asset_manager(const asset_manager&) = delete;
  asset_manager& operator=(const asset_manager&) = delete;
  [[nodiscard]] const asset_internal::virtual_file_system& files() const noexcept { return files_; }
  void collect_unused() noexcept {
    cache_.release_unused();
    if (documents_) {
      documents_->collect();
    }
  }
  /** 阻塞加载入口，供离线/兼容调用；交互路径使用请求通道。
   * 输出类型决定 Loader，所有类型共用本管理器缓存及既有引用语义。 */
  [[nodiscard]] gneiss_result acquire(std::string_view uri,
                                      render_internal::mesh_asset_lease& output,
                                      render_internal::asset_diagnostic& diagnostic) noexcept {
    return render_.acquire_mesh(uri, output, diagnostic);
  }
  [[nodiscard]] gneiss_result acquire(std::string_view uri,
                                      render_internal::material_asset_lease& output,
                                      render_internal::asset_diagnostic& diagnostic) noexcept {
    return render_.acquire_material(uri, output, diagnostic);
  }
  [[nodiscard]] gneiss_result acquire(std::string_view uri,
                                      render_internal::texture_asset_lease& output,
                                      render_internal::asset_diagnostic& diagnostic) noexcept {
    return render_.acquire_texture(uri, output, diagnostic);
  }
  [[nodiscard]] gneiss_result acquire(std::string_view uri,
                                      scene_internal::prefab_asset_lease& output,
                                      scene_internal::scene_diagnostic& diagnostic) noexcept {
    return prefabs_.acquire(uri, output, diagnostic);
  }
  [[nodiscard]] gneiss_result reload(std::span<const render_internal::render_asset_reload> sources,
                                     render_internal::asset_diagnostic& diagnostic) noexcept {
    return render_.reload_assets(sources, diagnostic);
  }
  /** 共享 CPU 文档缓存的所有权以装配请求服务；不提供上传或设备配置。 */
  [[nodiscard]] std::shared_ptr<asset_internal::cpu_document_cache> documents() const noexcept {
    return documents_;
  }
  [[nodiscard]] gneiss_result prepare_adoption(
      std::span<const asset_internal::resource_cache::version_selection> selections,
      std::unique_ptr<asset_internal::resource_cache::prepared_adoption>& output) noexcept {
    return cache_.prepare_adoption(selections, output);
  }
  /** 模块内装配借用；不暴露给安装 SDK。类型解析与候选构建仍由专用 Loader 执行。 */
  [[nodiscard]] render_internal::render_asset_loader& render_loader() noexcept { return render_; }
  [[nodiscard]] scene_internal::prefab_asset_loader& prefab_loader() noexcept { return prefabs_; }

private:
  struct resource_state;
  std::unique_ptr<resource_state> resource_state_;
  asset_internal::virtual_file_system files_;
  asset_internal::resource_cache cache_;
  std::shared_ptr<asset_internal::cpu_document_cache> documents_{
      asset_internal::make_cpu_document_cache()};
  render_internal::render_asset_loader render_;
  scene_internal::prefab_asset_loader prefabs_;
};
}

#endif
