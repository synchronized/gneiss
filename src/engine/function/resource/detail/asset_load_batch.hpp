// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/result.hpp"
#include "engine/core/tasks/task_scheduler.hpp"
#include "engine/function/resource/asset_manager.hpp"
#include "engine/function/resource/asset_types.hpp"
#include "engine/function/resource/detail/asset_decode.hpp"

#include <deque>
#include <optional>
#include <thread>

namespace gneiss::engine {

/** 管理器内部的主线程批次编排；文件服务读取字节，任务只解码已有内存数据。
 * 宿主统一分发文件完成；CPU 内容在所属线程采用，GPU 上传由 Render 独立安排。
 * 只接受一个在途批次；宿主保序排队，容量满返回 NOT_READY，不自动创建池。 */
class asset_load_batch final {
public:
  static constexpr std::size_t maximum_batch = 16U;
  static constexpr std::size_t maximum_assets = 256U;
  static constexpr std::size_t maximum_prepared_bytes = 256U * 1024U * 1024U;
  static constexpr std::size_t maximum_bytes = 64U * 1024U * 1024U;
  asset_load_batch(task_executor& executor, asset_manager& assets,
                   texture_prepare_profile profile = {},
                   std::shared_ptr<progress_notification> notification = {});
  ~asset_load_batch();
  [[nodiscard]] engine::result submit(std::span<const std::string> uris, std::uint64_t session,
                                      std::uint64_t revision, std::uint64_t& request,
                                      bool reload = true);
  [[nodiscard]] engine::result submit_assets(std::span<const asset_source> sources,
                                             std::uint64_t session, std::uint64_t revision,
                                             std::uint64_t& request, bool reload = true,
                                             std::size_t prepare_limit = maximum_prepared_bytes);
  [[nodiscard]] bool progress(asset_load_progress& output) const;
  /** 所属线程借用请求目标，普通模式提交成功即可取得；下一次推进/取回执后重新查询。
   * 需要长期引用的使用者复制租约；引用有效不代表内容已就绪。
   * 目标有效不代表 CPU/GPU 已就绪；失败目标在使用者释放租约后由缓存清理回收。 */
  [[nodiscard]] std::span<const render_asset_ref> requested_assets() const;
  [[nodiscard]] bool busy() const noexcept { return pending_ != nullptr || completed_.has_value(); }
  [[nodiscard]] texture_prepare_profile profile() const noexcept { return profile_; }
  void advance();
  [[nodiscard]] bool take(asset_load_completion& output);
  /** 取消尚未完成的 CPU 工作；已经采用的项保留。 */
  bool cancel();
  void request_stop();
  [[nodiscard]] bool stopped() const;

private:
  struct pending;
  submit_result submit_preparation(pending& value);
  bool advance_preparation(pending& value);
  engine::result enqueue_dependencies(pending& value, const prepared_asset& asset);
  void finish_item(engine::result result, bool applied);
  bool start_item(pending& value);
  result start_read(pending& value, std::string_view uri, bool payload);
  bool prepare_item_update(pending& value);
  void advance_impl();
  void check_owner() const;
  void finish(engine::result result, asset_load_state state);
  task_executor& executor_;
  task_scope scope_;
  asset_manager& assets_;
  texture_prepare_profile profile_;
  std::shared_ptr<progress_notification> notification_;
  std::unique_ptr<pending> pending_;
  std::optional<asset_load_completion> completed_;
  std::uint64_t sequence_{};
  bool stopping_{};
  const std::thread::id owner_{std::this_thread::get_id()};
};

} // namespace gneiss::engine
