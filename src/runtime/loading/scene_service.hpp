// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RUNTIME_SCENE_SERVICE_HPP_
#define GNEISS_RUNTIME_SCENE_SERVICE_HPP_

#include "engine/core/result.hpp"
#include "engine/core/rid.hpp"
#include "engine/core/tasks/task_scheduler.hpp"
#include "engine/function/render/texture_load_service.hpp"
#include "engine/function/world/world.hpp"
#include "runtime/loading/scene_domain.hpp"
#include "runtime/loading/scene_load_types.hpp"
#include <optional>
#include <thread>

namespace gneiss::runtime {

/** 场景生命周期的唯一拥有者；全部调用限宿主线程。
 * 借用的资产管理器、渲染服务与执行器必须存活至 shutdown 返回。
 * shutdown 可等待在途任务；须在关闭渲染后端和 I/O 执行器之前调用。
 */
class scene_service final {
public:
  scene_service();
  ~scene_service() noexcept;
  scene_service(const scene_service&) = delete;
  scene_service& operator=(const scene_service&) = delete;

  [[nodiscard]] gneiss::engine::result initialize(gneiss::engine::asset_manager& assets,
                                                  gneiss::engine::render_asset_loader& render);
  [[nodiscard]] gneiss::engine::result
  attach_executor(gneiss::engine::task_executor& executor,
                  gneiss::engine::render_resource_service& resources,
                  gneiss::engine::texture_upload_backend backend,
                  std::shared_ptr<gneiss::engine::progress_notification> notification);
  void advance();
  /** 激活后的视觉资源独立完成，不影响结构激活结果；所属线程消费诊断。 */
  [[nodiscard]] bool take_visual_completion(gneiss::engine::texture_load_completion& output);
  [[nodiscard]] bool visual_loading() const noexcept {
    return visual_pending_ || !visual_queue_.empty();
  }
  [[nodiscard]] gneiss::engine::result activate(std::uint64_t request,
                                                scene_load_completion& completion);
  void shutdown() noexcept;

  [[nodiscard]] bool can_start_load() const noexcept;
  [[nodiscard]] bool can_activate() const noexcept {
    return !retired_ && textures_ && !textures_->busy();
  }
  [[nodiscard]] bool loading() const noexcept;
  [[nodiscard]] scene_retirement_statistics retirement() const noexcept;
  [[nodiscard]] gneiss::engine::world* native_world() const noexcept;
  [[nodiscard]] gneiss::engine::scene_instances* scenes() noexcept;
  /** 内部适配借用；视觉服务在 attach_executor 后存活至 shutdown，不随场景切换重建。 */
  [[nodiscard]] gneiss::engine::texture_load_service* textures() noexcept;
  /** 请求、进度和完成值不暴露候选域；激活仍由宿主在安全点显式调用。 */
  [[nodiscard]] gneiss::engine::result request_scene(std::string_view uri, std::uint64_t session,
                                                     std::uint64_t revision,
                                                     std::uint64_t& request);
  [[nodiscard]] bool scene_progress(scene_load_progress& progress) const;
  [[nodiscard]] gneiss::engine::result poll_scene(scene_load_completion& completion, bool& ready);
  [[nodiscard]] gneiss::engine::result cancel_scene(std::uint64_t request);
  /** 更新稳定资源的内容，活动 World 的引用不变；失败项保留旧内容。 */
  [[nodiscard]] gneiss::engine::result
  request_assets(std::span<const gneiss::engine::render_asset_reload> sources,
                 std::uint64_t session, std::uint64_t revision, std::uint64_t& request,
                 bool reload);
  /** 所属线程、渲染快照捕获之前调用；回执报告内容更新结果，不改变场景引用。 */
  [[nodiscard]] gneiss::engine::result
  poll_assets(gneiss::engine::texture_load_completion& completion, bool& ready);

private:
  void advance_visuals();
  std::vector<gneiss::engine::render_asset_reload> visual_queue_;
  std::size_t visual_cursor_{};
  std::uint64_t visual_session_{};
  std::uint64_t visual_revision_{};
  bool visual_pending_{};
  std::optional<gneiss::engine::texture_load_completion> visual_completion_;
  gneiss::engine::asset_manager* asset_owner_{};
  gneiss::engine::render_asset_loader* render_assets_{};
  std::unique_ptr<scene_domain> active_;
  std::unique_ptr<scene_domain> retired_;
  scene_retirement_statistics retirement_;
  // 只在加载期间持有；完成回执消费前仍占用请求槽位。
  struct load_task;
  void advance_load();
  void advance_load_impl();
  void stop_load() noexcept;
  void instantiate_loaded(load_task& value);
  void finish(gneiss::engine::result result, scene_load_phase phase, std::string message = {});
  void sample_budget(scene_load_progress& value) const;
  void check_owner() const;
  gneiss::engine::task_executor* executor_{};
  gneiss::engine::task_scope scope_{};
  gneiss::engine::render_resource_service* resources_{};
  std::shared_ptr<gneiss::engine::progress_notification> notification_;
  std::unique_ptr<load_task> pending_;
  std::optional<scene_load_completion> completed_;
  std::uint64_t sequence_{};
  const std::thread::id owner_{std::this_thread::get_id()};
  std::unique_ptr<gneiss::engine::texture_load_service> textures_;
};

} // namespace gneiss::runtime

#endif
