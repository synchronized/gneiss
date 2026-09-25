// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_RUNTIME_RUNTIME_ASSET_RELOADER_H_
#define GNEISS_APPS_RUNTIME_RUNTIME_ASSET_RELOADER_H_

#include "ipc_asset_protocol.h"

#include <functional>
#include <optional>
#include <span>
#include <thread>

namespace gneiss::runtime_internal {

/** Runtime 主线程上的资产修订事务协调器。 */
class runtime_asset_reloader final {
public:
  using apply_function = std::function<result(std::span<const ipc_asset_revision>)>;
  using begin_function = std::function<result(const ipc_asset_reload_request&)>;
  using poll_function = std::function<result(result&, bool&)>;

  explicit runtime_asset_reloader(apply_function apply, begin_function begin = {},
                                  poll_function poll = {});
  /** 接受异步纹理请求后 execute 返回 not_ready；只有 advance 才生成终态。 */
  [[nodiscard]] result advance(ipc_asset_reload_result& response, bool& ready) noexcept;

  /** 应用新修订并生成权威结果；旧修订不会再次调用底层资源事务。 */
  [[nodiscard]] result execute(const ipc_asset_reload_request& request,
                               ipc_asset_reload_result& response) noexcept;

  [[nodiscard]] std::uint64_t session_id() const noexcept { return session_id_; }
  [[nodiscard]] std::uint64_t applied_revision() const noexcept { return applied_revision_; }

private:
  apply_function apply_;
  begin_function begin_;
  poll_function poll_;
  std::optional<ipc_asset_reload_request> pending_;
  std::thread::id owner_thread_;
  std::uint64_t session_id_ = 0U;
  std::uint64_t applied_revision_ = 0U;
};

} // namespace gneiss::runtime_internal

#endif
