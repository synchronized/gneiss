// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "runtime/loading/scene_load_types.hpp"

#include "engine/function/render/texture_load_service.hpp"
#include "engine/function/scene/scene_load_builder.hpp"
#include "runtime/loading/application_scene_state.hpp"

namespace gneiss::runtime {

/** 仅所属线程操作；使用宿主执行器，不新建线程。一个未消费请求占一个槽位。
 * ready 仍是私有域，宿主显式取得候选后才可在模块生命周期安全点切换。 */
class scene_load_service final {
public:
  scene_load_service(engine::core::tasks::task_executor& executor,
                     engine::resource::virtual_file_system files,
                     engine::render::render_resource_service& resources,
                     engine::resource::asset_manager& asset_owner,
                     engine::render::render_asset_loader& render_assets,
                     engine::render::texture_upload_backend backend,
                     std::shared_ptr<engine::core::progress_notification> notification = {});
  ~scene_load_service();
  [[nodiscard]] gneiss_result submit(std::string_view uri, std::uint64_t session,
                                     std::uint64_t revision, std::uint64_t& request);
  void advance();
  [[nodiscard]] bool progress(scene_load_progress& value) const;
  [[nodiscard]] bool busy() const noexcept { return pending_ != nullptr || completed_.has_value(); }
  [[nodiscard]] bool cancel(std::uint64_t request);
  /** 仅 ready 可取，取出后取消过晚；调用方必须完成不可失败的指针交换再报告 applied。 */
  [[nodiscard]] std::unique_ptr<application_scene_state>
  take_candidate(std::uint64_t request, scene_load_completion& result,
                 std::unique_ptr<engine::render::texture_load_service>& assets,
                 std::vector<engine::render::render_asset_reload>& requested);
  [[nodiscard]] bool take(scene_load_completion& result);

private:
  struct pending;
  void advance_impl();
  engine::core::tasks::submit_result submit_verification();
  void advance_verification(pending& value);
  void finish(gneiss_result result, scene_load_phase phase, std::string message = {});
  void sample_budget(scene_load_progress& value) const;
  void check_owner() const;
  engine::core::tasks::task_executor& executor_;
  engine::core::tasks::task_scope scope_;
  engine::resource::virtual_file_system files_;
  engine::render::render_resource_service& resources_;
  engine::resource::asset_manager& asset_owner_;
  engine::render::render_asset_loader& render_assets_;
  engine::render::texture_upload_backend backend_;
  std::shared_ptr<engine::core::progress_notification> notification_;
  std::unique_ptr<pending> pending_;
  std::optional<scene_load_completion> completed_;
  std::uint64_t sequence_{};
  const std::thread::id owner_{std::this_thread::get_id()};
};

} // namespace gneiss::runtime
