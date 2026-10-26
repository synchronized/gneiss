// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/result.hpp"
#include "engine/core/tasks/task_scheduler.hpp"
#include "engine/function/render/render_asset_loader.hpp"
#include "engine/resource/vfs/virtual_file_system.hpp"

#include <deque>
#include <optional>
#include <thread>

namespace gneiss::engine {

enum class texture_load_state : std::uint8_t { preparing, applied, failed, cancelled };
/** 请求中每项资源的结果；未生效项的 result 表示停止处理的原因，不表示旧内容失效。 */
struct asset_load_result {
  render_asset_reload source;
  gneiss::engine::result result{gneiss::engine::result::not_ready};
  bool applied{};
};
struct texture_load_completion {
  std::uint64_t request{};
  std::uint64_t session{};
  std::uint64_t revision{};
  texture_load_state state{texture_load_state::failed};
  gneiss::engine::result result{gneiss::engine::result::internal};
  std::string message{};
  std::vector<gneiss::engine::render_asset_ref> assets;
  /** 包含显式请求及已发现依赖；取消/失败也保留已成功项，元数据不延长资源寿命。 */
  std::vector<asset_load_result> results;
  /** 本次准备使用的设备能力。 */
  texture_prepare_profile profile{};
  double prepare_ms{};
  double queue_ms{};
  double commit_ms{};
  /** 本请求累计成功解码的字节估算，不是同时驻留内存。 */
  std::size_t prepared_bytes{};
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

/** 主线程服务；只读 VFS 副本和单项临时数据由任务拥有，CPU 内容采用留在所属线程，GPU 上传由 Render
 * 独立安排。 挂载后端在服务生存期内不得重配，read_bounded 须支持只读并发访问。
 * 只接受一个在途批次；宿主保序排队，容量满返回 NOT_READY，不自动创建池。 */
class texture_load_service final {
public:
  static constexpr std::size_t maximum_batch = 16U;
  static constexpr std::size_t maximum_assets = 256U;
  static constexpr std::size_t maximum_prepared_bytes = 256U * 1024U * 1024U;
  static constexpr std::size_t maximum_bytes = 64U * 1024U * 1024U;
  texture_load_service(gneiss::engine::task_executor& executor,
                       gneiss::engine::render_asset_loader& loader,
                       texture_prepare_profile profile = {},
                       std::shared_ptr<gneiss::engine::progress_notification> notification = {});
  ~texture_load_service();
  [[nodiscard]] gneiss::engine::result submit(std::span<const std::string> uris,
                                              std::uint64_t session, std::uint64_t revision,
                                              std::uint64_t& request, bool reload = true);
  [[nodiscard]] gneiss::engine::result
  submit_assets(std::span<const gneiss::engine::render_asset_reload> sources, std::uint64_t session,
                std::uint64_t revision, std::uint64_t& request, bool reload = true,
                std::size_t prepare_limit = maximum_prepared_bytes);
  [[nodiscard]] bool progress(asset_load_progress& output) const;
  /** 所属线程借用请求目标，普通模式提交成功即可取得；下一次推进/取回执后重新查询。
   * 需要长期引用的使用者复制租约；引用有效不代表内容已就绪。
   * 目标有效不代表 CPU/GPU 已就绪；失败目标在使用者释放租约后由缓存清理回收。 */
  [[nodiscard]] std::span<const render_asset_ref> requested_assets() const;
  [[nodiscard]] bool busy() const noexcept { return pending_ != nullptr || completed_.has_value(); }
  [[nodiscard]] texture_prepare_profile profile() const noexcept { return profile_; }
  void advance();
  [[nodiscard]] bool take(texture_load_completion& output);
  /** 取消尚未完成的 CPU 工作；已经采用的项保留。 */
  bool cancel();
  /** 停止后续读取与解码；已生效内容保留。 */
  bool cancel_unpublished();
  void request_stop();
  [[nodiscard]] bool stopped() const;

private:
  struct pending;
  gneiss::engine::submit_result submit_preparation(pending& value);
  bool advance_preparation(pending& value);
  gneiss::engine::result enqueue_dependencies(pending& value, const prepared_render_asset& asset);
  void finish_item(gneiss::engine::result result, bool applied);
  bool start_item(pending& value);
  result start_read(pending& value, std::string_view uri, bool payload);
  bool prepare_item_update(pending& value);
  void advance_impl();
  void check_owner() const;
  void finish(gneiss::engine::result result, texture_load_state state);
  gneiss::engine::task_executor& executor_;
  gneiss::engine::task_scope scope_;
  gneiss::engine::render_asset_loader& loader_;
  texture_prepare_profile profile_;
  std::shared_ptr<gneiss::engine::progress_notification> notification_;
  std::unique_ptr<pending> pending_;
  std::optional<texture_load_completion> completed_;
  std::uint64_t sequence_{};
  bool stopping_{};
  const std::thread::id owner_{std::this_thread::get_id()};
};

} // namespace gneiss::engine
