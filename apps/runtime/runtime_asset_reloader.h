// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_RUNTIME_RUNTIME_ASSET_RELOADER_H_
#define GNEISS_APPS_RUNTIME_RUNTIME_ASSET_RELOADER_H_

#include "ipc_asset_protocol.h"

#include <functional>
#include <span>
#include <thread>

namespace gneiss::runtime_internal {

/** Runtime 主线程上的资产修订事务协调器。 */
class runtime_asset_reloader final {
public:
  using apply_function = std::function<result(std::span<const ipc_asset_revision>)>;

  explicit runtime_asset_reloader(apply_function apply);

  /** 应用新修订并生成权威结果；旧修订不会再次调用底层资源事务。 */
  [[nodiscard]] result execute(const ipc_asset_reload_request& request,
                               ipc_asset_reload_result& response) noexcept;

  [[nodiscard]] std::uint64_t session_id() const noexcept { return session_id_; }
  [[nodiscard]] std::uint64_t applied_revision() const noexcept { return applied_revision_; }

private:
  apply_function apply_;
  std::thread::id owner_thread_;
  std::uint64_t session_id_ = 0U;
  std::uint64_t applied_revision_ = 0U;
};

} // namespace gneiss::runtime_internal

#endif
