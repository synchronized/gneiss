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

  [[nodiscard]] gneiss_result initialize(engine::asset_manager& assets);
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
  [[nodiscard]] engine::world* native_world() const noexcept;
  [[nodiscard]] scene_internal::scene_instance_service* scenes() noexcept;
  /** 内部适配借用；纹理服务在场景激活时替换，不得跨激活缓存。 */
  [[nodiscard]] render_internal::texture_load_service* textures() noexcept;
  /** 请求、进度和完成值不暴露候选域；激活仍由宿主在安全点显式调用。 */
  [[nodiscard]] gneiss_result request_scene(std::string_view uri, std::uint64_t session,
                                            std::uint64_t revision, std::uint64_t& request);
  [[nodiscard]] bool scene_progress(scene_load_progress& progress) const;
  [[nodiscard]] gneiss_result poll_scene(scene_load_completion& completion, bool& ready);
  [[nodiscard]] gneiss_result cancel_scene(std::uint64_t request);
  /** 重载固定版本并更新活动 World 的现有使用者；加载模式保留旧缓存行为。 */
  [[nodiscard]] gneiss_result
  request_assets(std::span<const render_internal::render_asset_reload> sources,
                 std::uint64_t session, std::uint64_t revision, std::uint64_t& request,
                 bool reload);
  /** 所属线程、渲染快照捕获之前调用；返回成功回执前完成场景引用切换。 */
  [[nodiscard]] gneiss_result poll_assets(render_internal::texture_load_completion& completion,
                                          bool& ready);
  [[nodiscard]] gneiss_result
  accept_edit_assets(const render_internal::texture_load_completion& completion,
                     const std::function<gneiss_result()>& accept);

private:
  engine::asset_manager* asset_owner_{};
  std::unique_ptr<application_scene_state> active_;
  std::unique_ptr<application_scene_state> retired_;
  scene_retirement_statistics retirement_;
  std::unique_ptr<scene_load_service> loader_;
  std::unique_ptr<render_internal::texture_load_service> textures_;
  std::uint64_t asset_request_{};
  std::vector<render_internal::render_asset_lease> previous_assets_;
};

} // namespace gneiss::application_internal

#endif
