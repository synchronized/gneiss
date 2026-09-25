// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "asset/virtual_file_system.h"
#include "core/tasks/task_scheduler.h"
#include "render/render_asset_loader.h"

#include <deque>
#include <optional>
#include <thread>

namespace gneiss::asset_internal {

enum class texture_load_state : std::uint8_t { preparing, uploading, applied, failed, cancelled };
struct texture_load_completion {
  std::uint64_t request{};
  std::uint64_t session{};
  std::uint64_t revision{};
  texture_load_state state{texture_load_state::failed};
  gneiss_result result{GNEISS_ERROR_INTERNAL};
  std::string message{};
  std::vector<render_internal::texture_asset_lease> textures;
  std::vector<render_internal::render_asset_lease> assets;
  double prepare_ms{};
  double queue_ms{};
  double upload_ms{};
  double commit_ms{};
  std::size_t candidate_bytes{};
};
struct texture_upload_backend {
  using data = std::vector<render_internal::render_upload_item>;
  std::function<gneiss_result(data, std::uint64_t&)> begin;
  std::function<bool(std::uint64_t, gneiss_result&)> poll;
  std::function<gneiss_result(data, std::uint64_t&)> discard;
  std::function<void()> flush;
  std::function<double()> elapsed_ms{};
  std::function<std::size_t(const render_internal::render_upload_item&)> estimate_bytes{};
};

struct asset_load_progress {
  std::uint64_t request{};
  std::uint64_t session{};
  std::uint64_t revision{};
  texture_load_state state{texture_load_state::preparing};
  std::size_t completed_assets{};
  std::size_t total_assets{};
  bool can_cancel{};
};

/** 主线程服务；只读 VFS 副本和候选由任务拥有，缓存及 GPU 操作仍留在所属线程。
 * 挂载后端在服务生存期内不得重配，read_bounded 须支持只读并发访问。
 * 只接受一个在途批次；宿主保序排队，容量满返回 NOT_READY，不自动创建池。 */
class texture_load_service final {
public:
  static constexpr std::size_t maximum_batch = 16U;
  static constexpr std::size_t maximum_assets = 256U;
  static constexpr std::size_t maximum_candidate_bytes = 256U * 1024U * 1024U;
  static constexpr std::size_t upload_budget_bytes = 8U * 1024U * 1024U;
  static constexpr std::size_t maximum_bytes = 64U * 1024U * 1024U;
  texture_load_service(tasks::task_executor& executor, virtual_file_system file_system,
                       render_internal::render_asset_loader& loader,
                       texture_upload_backend backend);
  ~texture_load_service();
  [[nodiscard]] gneiss_result submit(std::span<const std::string> uris, std::uint64_t session,
                                     std::uint64_t revision, std::uint64_t& request,
                                     bool reload = true);
  [[nodiscard]] gneiss_result
  submit_assets(std::span<const render_internal::render_asset_reload> assets, std::uint64_t session,
                std::uint64_t revision, std::uint64_t& request, bool reload = true,
                std::size_t prepare_limit = maximum_candidate_bytes);
  [[nodiscard]] bool progress(asset_load_progress& output) const;
  [[nodiscard]] bool busy() const noexcept { return pending_ != nullptr || completed_.has_value(); }
  void advance();
  [[nodiscard]] bool take(texture_load_completion& output);
  /** 提交许可前取消；已进入 GPU 阶段的批次完成或回滚。 */
  bool cancel();
  void request_stop();
  [[nodiscard]] bool stopped() const;

private:
  struct pending;
  void advance_impl();
  void check_owner() const;
  void finish(gneiss_result result, texture_load_state state);
  tasks::task_executor& executor_;
  tasks::task_scope scope_;
  virtual_file_system file_system_;
  render_internal::render_asset_loader& loader_;
  texture_upload_backend backend_;
  std::unique_ptr<pending> pending_;
  std::optional<texture_load_completion> completed_;
  std::uint64_t sequence_{};
  bool stopping_{};
  const std::thread::id owner_{std::this_thread::get_id()};
};

} // namespace gneiss::asset_internal
