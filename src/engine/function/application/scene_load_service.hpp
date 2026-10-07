// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/function/application/application_scene_state.hpp"
#include "engine/function/render/texture_load_service.hpp"
#include "engine/function/scene/scene_load_builder.hpp"

namespace gneiss::application_internal {

enum class scene_load_phase : std::uint8_t {
  preparing,
  assets,
  verifying,
  instantiating,
  ready,
  applied,
  failed,
  cancelled
};
struct scene_load_progress {
  std::uint64_t request{};
  std::uint64_t session{};
  std::uint64_t revision{};
  scene_load_phase phase{scene_load_phase::preparing};
  std::size_t completed{};
  std::size_t total{};
  /** 去重后的候选逻辑容量；纹理释放 CPU 负载后仍计入所选负载，约束 2 GiB 上限。 */
  std::size_t resident_bytes{};
  bool can_cancel{};
  bool gpu_in_flight{};
  /** 候选已提交 CPU 数组字节与纹理逻辑负载；不是 RSS 或驱动显存。 */
  std::size_t cpu_data_bytes{};
  std::size_t texture_payload_bytes{};
  std::size_t peak_upload_bytes{};
  /** Application 资源账本快照；不含后台临时分配或驱动额外占用。 */
  std::uint64_t application_logical_bytes{};
  std::uint64_t application_cpu_data_bytes{};
  std::uint64_t available_bytes{};
  std::uint64_t upload_reserved_bytes{};
  /** 失败/取消后请求容器与上传租约已释放；不表示活动场景或旧帧占用归零。 */
  bool cleanup_complete{};
  /** 取消已确认，但上传回执与候选清理仍在推进；此时不得重试。 */
  bool cleanup_pending{};
};
struct scene_load_completion {
  scene_load_progress progress;
  gneiss_result result{GNEISS_ERROR_INTERNAL};
  std::string message;
  gneiss_scene_instance scene{};
  double prepare_ms{};
  double asset_prepare_ms{};
  double verify_ms{};
  double verify_maximum_step_ms{};
  double verify_maximum_open_ms{};
  double verify_maximum_read_ms{};
  double verify_maximum_hash_ms{};
  double upload_ms{};
  double maximum_advance_ms{};
  double activation_ms{};
  double cleanup_ms{};
};

struct scene_retirement_statistics {
  double last_ms{};
  std::uint64_t retired_domains{};
  std::size_t live_resources{};
  bool pending{};
};

/** 仅所属线程操作；使用宿主执行器，不新建线程。一个未消费请求占一个槽位。
 * ready 仍是私有域，宿主显式取得候选后才可在模块生命周期安全点切换。 */
class scene_load_service final {
public:
  static constexpr std::size_t maximum_resident_bytes = 2ULL * 1024U * 1024U * 1024U;
  scene_load_service(tasks::task_executor& executor, asset_internal::virtual_file_system files,
                     render_internal::render_resource_service& resources,
                     render_internal::texture_upload_backend backend);
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
                 std::unique_ptr<render_internal::texture_load_service>& assets);
  [[nodiscard]] bool take(scene_load_completion& result);

private:
  struct pending;
  void advance_impl();
  tasks::submit_result submit_verification();
  void advance_verification(pending& value);
  void finish(gneiss_result result, scene_load_phase phase, std::string message = {});
  void sample_budget(scene_load_progress& value) const;
  void check_owner() const;
  tasks::task_executor& executor_;
  tasks::task_scope scope_;
  asset_internal::virtual_file_system files_;
  render_internal::render_resource_service& resources_;
  render_internal::texture_upload_backend backend_;
  std::unique_ptr<pending> pending_;
  std::optional<scene_load_completion> completed_;
  std::optional<scene_load_progress> cleanup_snapshot_;
  std::uint64_t sequence_{};
  const std::thread::id owner_{std::this_thread::get_id()};
};

} // namespace gneiss::application_internal
