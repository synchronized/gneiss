// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/tasks/task_scheduler.hpp"
#include "engine/function/render/render_asset_loader.hpp"
#include "engine/resource/vfs/virtual_file_system.hpp"

#include <deque>
#include <optional>
#include <thread>

namespace gneiss::engine::resource {
class cpu_document_cache;
}

namespace gneiss::engine::render {

enum class texture_load_state : std::uint8_t { preparing, uploading, applied, failed, cancelled };
/** 请求中每项资源的结果；未生效项的 result 表示停止处理的原因，不表示旧内容失效。 */
struct asset_load_result {
  render_asset_reload source;
  gneiss_result result{GNEISS_ERROR_NOT_READY};
  bool applied{};
};
struct texture_load_completion {
  std::uint64_t request{};
  std::uint64_t session{};
  std::uint64_t revision{};
  texture_load_state state{texture_load_state::failed};
  gneiss_result result{GNEISS_ERROR_INTERNAL};
  std::string message{};
  std::vector<engine::render::render_asset_lease> assets;
  /** 包含显式请求及已发现依赖；取消/失败也保留已成功项，元数据不延长资源寿命。 */
  std::vector<asset_load_result> results;
  /** 本次准备使用的设备能力。 */
  texture_prepare_profile profile{};
  double prepare_ms{};
  double queue_ms{};
  double upload_ms{};
  double commit_ms{};
  std::size_t candidate_bytes{};
  /** 后端估算的最大单次在途上传字节，不是驱动显存统计。 */
  std::size_t peak_upload_bytes{};
};
struct texture_upload_backend {
  using data = std::vector<engine::render::render_upload_item>;
  std::function<gneiss_result(data, std::uint64_t&)> begin;
  std::function<bool(std::uint64_t, gneiss_result&)> poll;
  std::function<gneiss_result(data, std::uint64_t&)> discard;
  std::function<void()> flush;
  std::function<double()> elapsed_ms{};
  std::function<std::size_t(const engine::render::render_upload_item&)> estimate_bytes{};
  engine::render::texture_prepare_profile profile{};
};

struct asset_load_progress {
  std::uint64_t request{};
  std::uint64_t session{};
  std::uint64_t revision{};
  texture_load_state state{texture_load_state::preparing};
  std::size_t completed_assets{};
  std::size_t total_assets{};
  bool can_cancel{};
  std::size_t upload_reserved_bytes{};
};

/** 主线程服务；只读 VFS 副本和候选由任务拥有，缓存及 GPU 操作仍留在所属线程。
 * 挂载后端在服务生存期内不得重配，read_bounded 须支持只读并发访问。
 * 只接受一个在途批次；宿主保序排队，容量满返回 NOT_READY，不自动创建池。 */
class texture_load_service final {
public:
  static constexpr std::size_t maximum_batch = 16U;
  static constexpr std::size_t maximum_assets = 256U;
  static constexpr std::size_t maximum_candidate_bytes = 256U * 1024U * 1024U;
  // 8 MiB 为分批目标；不可拆单资产允许超出，但不得超过上传硬上限。
  static constexpr std::size_t upload_budget_bytes = 8U * 1024U * 1024U;
  static constexpr std::size_t maximum_upload_bytes = maximum_candidate_bytes;
  static constexpr std::size_t maximum_bytes = 64U * 1024U * 1024U;
  texture_load_service(engine::core::tasks::task_executor& executor,
                       engine::resource::virtual_file_system file_system,
                       engine::render::render_asset_loader& loader, texture_upload_backend backend,
                       std::shared_ptr<engine::core::progress_notification> notification = {},
                       std::shared_ptr<engine::resource::cpu_document_cache> documents = {});
  ~texture_load_service();
  [[nodiscard]] gneiss_result submit(std::span<const std::string> uris, std::uint64_t session,
                                     std::uint64_t revision, std::uint64_t& request,
                                     bool reload = true);
  [[nodiscard]] gneiss_result
  submit_assets(std::span<const engine::render::render_asset_reload> sources, std::uint64_t session,
                std::uint64_t revision, std::uint64_t& request, bool reload = true,
                std::size_t prepare_limit = maximum_candidate_bytes);
  [[nodiscard]] bool progress(asset_load_progress& output) const;
  /** 所属线程借用请求目标，普通模式提交成功即可取得；下一次推进/取回执后重新查询。
   * 需要长期引用的使用者复制租约；引用有效不代表内容已就绪。
   * 目标有效不代表 CPU/GPU 已就绪；失败目标在使用者释放租约后由缓存清理回收。 */
  [[nodiscard]] std::span<const render_asset_lease> requested_assets() const;
  [[nodiscard]] bool busy() const noexcept { return pending_ != nullptr || completed_.has_value(); }
  [[nodiscard]] texture_prepare_profile profile() const noexcept { return backend_.profile; }
  void advance();
  [[nodiscard]] bool take(texture_load_completion& output);
  /** 提交许可前取消；已进入 GPU 阶段后须等待结果或使用 cancel_unpublished。 */
  bool cancel();
  /** 停止后续上传并安全清理未生效内容；已生效内容保留。 */
  bool cancel_unpublished();
  void request_stop();
  [[nodiscard]] bool stopped() const;

private:
  struct pending;
  engine::core::tasks::submit_result submit_preparation(pending& value);
  bool advance_preparation(pending& value);
  void advance_impl();
  void check_owner() const;
  void finish(gneiss_result result, texture_load_state state);
  engine::core::tasks::task_executor& executor_;
  engine::core::tasks::task_scope scope_;
  engine::resource::virtual_file_system file_system_;
  engine::render::render_asset_loader& loader_;
  texture_upload_backend backend_;
  std::shared_ptr<engine::core::progress_notification> notification_;
  std::shared_ptr<engine::resource::cpu_document_cache> documents_;
  std::unique_ptr<pending> pending_;
  std::optional<texture_load_completion> completed_;
  std::uint64_t sequence_{};
  bool stopping_{};
  const std::thread::id owner_{std::this_thread::get_id()};
};

} // namespace gneiss::engine::render
