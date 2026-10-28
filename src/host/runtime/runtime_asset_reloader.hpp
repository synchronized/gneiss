// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_RUNTIME_RUNTIME_ASSET_RELOADER_H_
#define GNEISS_APPS_RUNTIME_RUNTIME_ASSET_RELOADER_H_

#include "engine/core/error.hpp"
#include "ipc_asset_protocol.hpp"

#include <functional>
#include <optional>
#include <span>
#include <thread>

namespace gneiss::host {

/** Runtime 主线程上的资产重载请求协调器。 */
class runtime_asset_reloader final {
public:
  using begin_function = std::function<gneiss::engine::error(const ipc_asset_reload_request&)>;
  using poll_function = std::function<gneiss::engine::error(
      gneiss::engine::error&, std::vector<ipc_asset_item_result>&, bool&)>;

  explicit runtime_asset_reloader(begin_function begin, poll_function poll);
  /** 所有资产只经 begin/poll 处理；接受后 execute 返回 not_ready。
   * 只有 advance 消费终态后才更新已应用修订。 */
  [[nodiscard]] gneiss::engine::error advance(ipc_asset_reload_result& response,
                                              bool& ready) noexcept;

  /** 应用新修订并生成权威结果；旧修订不会再次调用底层资源请求。 */
  [[nodiscard]] gneiss::engine::error execute(const ipc_asset_reload_request& request,
                                              ipc_asset_reload_result& response) noexcept;

  [[nodiscard]] std::uint64_t session_id() const noexcept { return session_id_; }
  [[nodiscard]] std::uint64_t applied_revision() const noexcept { return applied_revision_; }

private:
  begin_function begin_;
  poll_function poll_;
  std::optional<ipc_asset_reload_request> pending_;
  std::thread::id owner_thread_;
  std::uint64_t session_id_ = 0U;
  std::uint64_t applied_revision_ = 0U;
};

} // namespace gneiss::host

#endif
