// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "application/application_scene_state.h"
#include "asset/texture_load_service.h"
#include "scene/scene_load_builder.h"

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
  std::size_t resident_bytes{};
  bool can_cancel{};
};
struct scene_load_completion {
  scene_load_progress progress;
  gneiss_result result{GNEISS_ERROR_INTERNAL};
  std::string message;
  gneiss_scene_instance scene{};
  double prepare_ms{};
  double asset_prepare_ms{};
  double verify_ms{};
  double upload_ms{};
  double maximum_advance_ms{};
  double activation_ms{};
};

/** 仅所属线程操作；使用宿主执行器，不新建线程。一个未消费请求占一个槽位。
 * ready 仍是私有域，宿主显式取得候选后才可在模块生命周期安全点切换。 */
class scene_load_service final {
public:
  static constexpr std::size_t maximum_resident_bytes = 2ULL * 1024U * 1024U * 1024U;
  scene_load_service(tasks::task_executor& executor, asset_internal::virtual_file_system files,
                     render_internal::render_resource_service& resources,
                     asset_internal::texture_upload_backend backend);
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
                 std::unique_ptr<asset_internal::texture_load_service>& assets);
  [[nodiscard]] bool take(scene_load_completion& result);

private:
  struct pending;
  void advance_impl();
  void finish(gneiss_result result, scene_load_phase phase, std::string message = {});
  void check_owner() const;
  tasks::task_executor& executor_;
  tasks::task_scope scope_;
  asset_internal::virtual_file_system files_;
  render_internal::render_resource_service& resources_;
  asset_internal::texture_upload_backend backend_;
  std::unique_ptr<pending> pending_;
  std::optional<scene_load_completion> completed_;
  std::uint64_t sequence_{};
  const std::thread::id owner_{std::this_thread::get_id()};
};

} // namespace gneiss::application_internal
