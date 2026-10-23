// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPLICATION_SCENE_SESSION_HPP_
#define GNEISS_APPLICATION_SCENE_SESSION_HPP_

#include "engine/function/render/texture_load_service.hpp"
#include "runtime/loading/application_scene_state.hpp"
#include "runtime/loading/scene_load_service.hpp"

namespace gneiss::runtime {

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

  [[nodiscard]] gneiss_result initialize(engine::resource::asset_manager& assets,
                                         engine::render::render_asset_loader& render);
  [[nodiscard]] gneiss_result
  attach_executor(engine::core::tasks::task_executor& executor,
                  const engine::resource::virtual_file_system& files,
                  engine::render::render_resource_service& resources,
                  engine::render::texture_upload_backend backend,
                  std::shared_ptr<engine::core::progress_notification> notification);
  void advance();
  /** 激活后的视觉资源独立完成，不影响结构激活结果；所属线程消费诊断。 */
  [[nodiscard]] bool take_visual_completion(engine::render::texture_load_completion& output);
  [[nodiscard]] bool visual_loading() const noexcept {
    return visual_pending_ || !visual_queue_.empty();
  }
  [[nodiscard]] gneiss_result activate(std::uint64_t request, scene_load_completion& completion);
  void shutdown() noexcept;

  [[nodiscard]] bool can_start_load() const noexcept;
  [[nodiscard]] bool can_activate() const noexcept {
    return !retired_ && textures_ && !textures_->busy();
  }
  [[nodiscard]] bool loading() const noexcept;
  [[nodiscard]] scene_retirement_statistics retirement() const noexcept;
  [[nodiscard]] gneiss_world world() const noexcept;
  [[nodiscard]] engine::world::world* native_world() const noexcept;
  [[nodiscard]] engine::scene::scene_instance_service* scenes() noexcept;
  /** 内部适配借用；视觉服务在 attach_executor 后存活至 shutdown，不随场景切换重建。 */
  [[nodiscard]] engine::render::texture_load_service* textures() noexcept;
  /** 请求、进度和完成值不暴露候选域；激活仍由宿主在安全点显式调用。 */
  [[nodiscard]] gneiss_result request_scene(std::string_view uri, std::uint64_t session,
                                            std::uint64_t revision, std::uint64_t& request);
  [[nodiscard]] bool scene_progress(scene_load_progress& progress) const;
  [[nodiscard]] gneiss_result poll_scene(scene_load_completion& completion, bool& ready);
  [[nodiscard]] gneiss_result cancel_scene(std::uint64_t request);
  /** 更新稳定资源的内容，活动 World 的引用不变；失败项保留旧内容。 */
  [[nodiscard]] gneiss_result
  request_assets(std::span<const engine::render::render_asset_reload> sources,
                 std::uint64_t session, std::uint64_t revision, std::uint64_t& request,
                 bool reload);
  /** 所属线程、渲染快照捕获之前调用；回执报告内容更新结果，不改变场景引用。 */
  [[nodiscard]] gneiss_result poll_assets(engine::render::texture_load_completion& completion,
                                          bool& ready);

private:
  void advance_visuals();
  std::vector<engine::render::render_asset_reload> visual_queue_;
  std::size_t visual_cursor_{};
  std::uint64_t visual_session_{};
  std::uint64_t visual_revision_{};
  bool visual_pending_{};
  std::optional<engine::render::texture_load_completion> visual_completion_;
  engine::resource::asset_manager* asset_owner_{};
  engine::render::render_asset_loader* render_assets_{};
  std::unique_ptr<application_scene_state> active_;
  std::unique_ptr<application_scene_state> retired_;
  scene_retirement_statistics retirement_;
  std::unique_ptr<scene_load_service> loader_;
  std::unique_ptr<engine::render::texture_load_service> textures_;
};

} // namespace gneiss::runtime

#endif
