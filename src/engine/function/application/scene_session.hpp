// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPLICATION_SCENE_SESSION_HPP_
#define GNEISS_APPLICATION_SCENE_SESSION_HPP_

#include "engine/function/application/application_scene_state.hpp"
#include "engine/function/application/scene_load_service.hpp"

namespace gneiss::application_internal {

/** 场景生命周期的唯一拥有者；全部调用限宿主线程。
 * 借用的文件系统、渲染服务与执行器必须存活至 shutdown 返回。
 * shutdown 可等待在途任务；须在关闭渲染后端和 I/O 执行器之前调用。
 */
class scene_session final {
public:
  scene_session() = default;
  ~scene_session() noexcept;
  scene_session(const scene_session&) = delete;
  scene_session& operator=(const scene_session&) = delete;

  [[nodiscard]] gneiss_result initialize(const asset_internal::virtual_file_system& files,
                                         render_internal::render_resource_service& resources);
  [[nodiscard]] gneiss_result
  attach_executor(tasks::task_executor& executor, const asset_internal::virtual_file_system& files,
                  render_internal::render_resource_service& resources,
                  render_internal::texture_upload_backend backend,
                  std::shared_ptr<core::progress_notification> notification);
  void advance();
  [[nodiscard]] gneiss_result activate(std::uint64_t request, scene_load_completion& completion);
  void shutdown() noexcept;

  [[nodiscard]] bool can_start_load() const noexcept;
  [[nodiscard]] bool loading() const noexcept;
  [[nodiscard]] scene_retirement_statistics retirement() const noexcept;
  [[nodiscard]] gneiss_world world() const noexcept;
  [[nodiscard]] scene_internal::scene_instance_service* scenes() noexcept;
  /** 要求 initialize 成功，借用至切换或 shutdown；不得跨帧缓存。 */
  [[nodiscard]] render_internal::render_asset_loader& assets() noexcept;
  /** 内部适配借用；纹理服务在场景激活时替换，不得跨激活缓存。 */
  [[nodiscard]] render_internal::texture_load_service* textures() noexcept;
  [[nodiscard]] scene_load_service* loader() noexcept;

private:
  std::unique_ptr<application_scene_state> active_;
  std::unique_ptr<application_scene_state> retired_;
  scene_retirement_statistics retirement_;
  std::unique_ptr<scene_load_service> loader_;
  std::unique_ptr<render_internal::texture_load_service> textures_;
};

} // namespace gneiss::application_internal

#endif
