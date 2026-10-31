// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RUNTIME_SCENE_SERVICE_HPP_
#define GNEISS_RUNTIME_SCENE_SERVICE_HPP_

#include "engine/core/error.hpp"
#include "engine/core/rid.hpp"
#include "engine/core/tasks/task_scheduler.hpp"
#include "engine/function/resource/resource_service.hpp"
#include "engine/function/scene/scene_preparation.hpp"
#include "engine/function/world/world.hpp"
#include "runtime/loading/resource_load_group.hpp"
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

  [[nodiscard]] gneiss::engine::error initialize(gneiss::engine::resource_service& resources);
  [[nodiscard]] gneiss::engine::error
  attach_executor(gneiss::engine::task_executor& executor, gneiss::engine::render_service& renderer,
                  gneiss::engine::texture_prepare_profile profile,
                  std::shared_ptr<gneiss::engine::progress_notification> notification);
  void advance();
  /** 激活后的视觉资源独立完成，不影响结构激活结果；所属线程消费诊断。 */
  [[nodiscard]] bool take_visual_completion(gneiss::runtime::asset_load_completion& output);
  [[nodiscard]] bool visual_loading() const noexcept {
    return visual_pending_ || !visual_queue_.empty();
  }
  [[nodiscard]] gneiss::engine::error activate(std::uint64_t request,
                                               scene_load_completion& completion);
  /** 请求停止加载，不等待 CPU/I/O；继续 advance 至 stopped，再释放依赖。 */
  void request_stop();
  [[nodiscard]] bool stopped() const noexcept;
  void shutdown() noexcept;

  [[nodiscard]] bool can_start_load() const noexcept;
  [[nodiscard]] bool can_activate() const noexcept {
    return !stopping_ && !reload_ && !retired_ && loading_assets() && !loading_assets()->busy();
  }
  [[nodiscard]] bool loading() const noexcept;
  [[nodiscard]] scene_retirement_statistics retirement() const noexcept;
  [[nodiscard]] gneiss::engine::world* native_world() const noexcept;
  [[nodiscard]] gneiss::engine::scene_instances* scenes() noexcept;
  /** 内部适配借用；视觉服务在 attach_executor 后存活至 shutdown，不随场景切换重建。 */
  [[nodiscard]] resource_load_group* loading_assets() const noexcept;
  /** 请求、进度和完成值不暴露候选域；激活仍由宿主在安全点显式调用。 */
  [[nodiscard]] gneiss::engine::error request_scene(std::string_view uri, std::uint64_t session,
                                                    std::uint64_t revision, std::uint64_t& request);
  [[nodiscard]] bool scene_progress(scene_load_progress& progress) const;
  [[nodiscard]] gneiss::engine::error poll_scene(scene_load_completion& completion, bool& ready);
  [[nodiscard]] gneiss::engine::error cancel_scene(std::uint64_t request);
  /** 更新稳定资源的内容，活动 World 的引用不变；失败项保留旧内容。 */
  [[nodiscard]] gneiss::engine::error
  request_assets(std::span<const gneiss::engine::render_resource_source> sources,
                 std::uint64_t session, std::uint64_t revision, std::uint64_t& request,
                 bool reload);
  /** 所属线程、渲染快照捕获之前调用；回执报告内容更新结果，不改变场景引用。 */
  [[nodiscard]] gneiss::engine::error
  poll_assets(gneiss::runtime::asset_load_completion& completion, bool& ready);

  /** 异步读取结构与依赖，所属线程原位修改实例；不切换 World。
   * 重载期间拒绝其他加载；poll_reload 消费终态后释放请求槽位。 */
  [[nodiscard]] engine::error request_reload(scene_instance_id instance, std::string_view uri,
                                             bool prefab);
  [[nodiscard]] engine::error poll_reload(engine::error& result, bool& ready);

private:
  struct reload_task {
    scene_instance_id instance;
    std::string uri;
    bool prefab{};
    std::unique_ptr<engine::scene_description_request> descriptions;
    engine::scene_description_inputs inputs;
    engine::scene_instantiation_data prepared;
    std::vector<engine::render_resource_ref> retained;
    std::size_t cursor{};
    bool asset_pending{};
    engine::error result{engine::error::not_ready};
  };
  void advance_reload();
  std::unique_ptr<reload_task> reload_;
  void advance_visuals();
  void advance_stop();
  bool stopping_{};
  std::vector<gneiss::engine::render_resource_source> visual_queue_;
  std::size_t visual_cursor_{};
  std::uint64_t visual_session_{};
  std::uint64_t visual_revision_{};
  bool visual_pending_{};
  std::optional<gneiss::runtime::asset_load_completion> visual_completion_;
  gneiss::engine::resource_service* resources_{};
  std::unique_ptr<scene_domain> active_;
  std::unique_ptr<scene_domain> retired_;
  scene_retirement_statistics retirement_;
  // 只在加载期间持有；完成回执消费前仍占用请求槽位。
  struct load_task;
  void advance_load();
  void advance_load_impl();
  void stop_load() noexcept;
  void instantiate_loaded(load_task& value);
  void finish(gneiss::engine::error result, scene_load_phase phase, std::string message = {});
  void sample_budget(scene_load_progress& value) const;
  void check_owner() const;
  gneiss::engine::task_executor* executor_{};
  gneiss::engine::task_scope scope_{};
  gneiss::engine::render_service* renderer_{};
  std::shared_ptr<gneiss::engine::progress_notification> notification_;
  std::unique_ptr<load_task> pending_;
  std::optional<scene_load_completion> completed_;
  std::uint64_t sequence_{};
  const std::thread::id owner_{std::this_thread::get_id()};
  std::unique_ptr<resource_load_group> loading_;
};

} // namespace gneiss::runtime

#endif
