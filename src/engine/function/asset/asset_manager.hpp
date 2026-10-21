// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#ifndef GNEISS_ENGINE_ASSET_MANAGER_HPP_
#define GNEISS_ENGINE_ASSET_MANAGER_HPP_

#include "engine/asset/cpu_document_cache.hpp"
#include "engine/asset/resource_cache.hpp"
#include "engine/asset/virtual_file_system.hpp"
#include "engine/function/input/action_map.hpp"
#include "engine/function/render/texture_load_service.hpp"
#include "engine/function/scene/prefab_asset_loader.hpp"
#include <functional>
#include <memory>
#include <new>
#include <utility>

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
  void attach_input_loader(tasks::task_executor& executor);
  /** 同步准备会取消旧异步采用；失败不改输出，也不驱动宿主任务队列。 */
  [[nodiscard]] result load_input(std::string_view uri, input_internal::action_map& output) noexcept;
  [[nodiscard]] result begin_input(std::string_view uri) noexcept;
  [[nodiscard]] result input_status() const noexcept;
  [[nodiscard]] result cancel_input() noexcept;
  void advance_input(const std::function<result(input_internal::action_map)>& apply);
  /** 先于场景、执行器与 I/O 后端关闭，取消并排空资源请求。 */
  void stop_input() noexcept;
  asset_manager(const asset_manager&) = delete;
  asset_manager& operator=(const asset_manager&) = delete;
  [[nodiscard]] const asset_internal::virtual_file_system& files() const noexcept { return files_; }
  void collect_unused() noexcept {
    cache_.release_unused();
    if (documents_) {
      documents_->collect();
    }
  }
  void set_profile(render_internal::texture_prepare_profile profile) noexcept {
    render_.set_default_profile(profile);
  }
  [[nodiscard]] gneiss_result
  query_ready(const render_internal::render_asset_reload& source,
              render_internal::texture_prepare_profile profile,
              render_internal::render_asset_lease& output) const noexcept {
    return render_.acquire_selected(source, profile, output);
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
  /** 创建独立请求通道，供编辑或隔离候选使用；通道先于管理器销毁。 */
  [[nodiscard]] std::unique_ptr<render_internal::texture_load_service>
  create_channel(tasks::task_executor& executor, asset_internal::virtual_file_system files,
                 render_internal::texture_upload_backend backend,
                 std::shared_ptr<core::progress_notification> notification = {}) {
    return std::make_unique<render_internal::texture_load_service>(
        executor, std::move(files), render_, std::move(backend), std::move(notification),
        documents_);
  }
  [[nodiscard]] gneiss_result prepare_adoption(
      std::span<const asset_internal::resource_cache::version_selection> selections,
      std::unique_ptr<asset_internal::resource_cache::prepared_adoption>& output) noexcept {
    return cache_.prepare_adoption(selections, output);
  }
  /** 采用加载结果时才改变默认版本；接受者失败必须保持使用者原状态。 */
  [[nodiscard]] gneiss_result accept(const render_internal::texture_load_completion& completion,
                                     const std::function<gneiss_result()>& apply) noexcept try {
    if (completion.result != GNEISS_SUCCESS || completion.versions.empty() ||
        completion.versions.size() != completion.assets.size()) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    std::vector<asset_internal::resource_cache::version_selection> selections;
    selections.reserve(completion.versions.size());
    for (std::size_t i = 0; i < completion.versions.size(); ++i) {
      if (!render_.owns(completion.assets[i])) {
        return GNEISS_ERROR_INVALID_ARGUMENT;
      }
      const auto& version = completion.versions[i];
      selections.push_back({
          .selection = render_internal::render_asset_loader::selection_key(version.identity,
                                                                           completion.profile),
          .version = version,
      });
    }
    return cache_.adopt_versions(selections, apply);
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
  /** 模块内装配借用；不暴露给安装 SDK。类型解析与候选构建仍由专用 Loader 执行。 */
  [[nodiscard]] render_internal::render_asset_loader& render_loader() noexcept { return render_; }
  [[nodiscard]] scene_internal::prefab_asset_loader& prefab_loader() noexcept { return prefabs_; }

private:
  struct input_state;
  std::unique_ptr<input_state> input_;
  asset_internal::virtual_file_system files_;
  asset_internal::resource_cache cache_;
  std::shared_ptr<asset_internal::cpu_document_cache> documents_{
      asset_internal::make_cpu_document_cache()};
  render_internal::render_asset_loader render_;
  scene_internal::prefab_asset_loader prefabs_;
};
}

#endif
