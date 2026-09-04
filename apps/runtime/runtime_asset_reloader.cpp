// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_asset_reloader.h"

#include <gneiss/core/result.hpp>

#include <utility>

namespace gneiss::runtime_internal {

runtime_asset_reloader::runtime_asset_reloader(apply_function apply)
    : apply_(std::move(apply)), owner_thread_(std::this_thread::get_id()) {}

result runtime_asset_reloader::execute(const ipc_asset_reload_request& request,
                                       ipc_asset_reload_result& response) noexcept {
  response = {.session_id = request.session_id,
              .revision = request.revision,
              .status = ipc_asset_apply_status::failed,
              .message = {}};
  if (!apply_ || request.session_id == 0U || request.revision == 0U || request.assets.empty()) {
    response.message = "资产重载请求无效";
    return result::invalid_argument;
  }
  if (std::this_thread::get_id() != owner_thread_) {
    response.message = "资产重载只能在 Runtime 主线程执行";
    return result::invalid_state;
  }
  if (session_id_ != 0U && request.session_id != session_id_) {
    session_id_ = request.session_id;
    applied_revision_ = 0U;
  } else if (session_id_ == 0U) {
    session_id_ = request.session_id;
  }
  if (request.revision <= applied_revision_) {
    response.status = ipc_asset_apply_status::stale;
    response.message = "忽略不晚于当前状态的资产修订";
    return result::success;
  }
  const auto applied = apply_(request.assets);
  if (applied == result::success) {
    applied_revision_ = request.revision;
    response.status = ipc_asset_apply_status::applied;
    response.message = "资产修订已应用";
    return result::success;
  }
  if (applied == result::unsupported) {
    response.status = ipc_asset_apply_status::restart_required;
    response.message = "该资产修订需要重启 Runtime";
    return result::success;
  }
  response.message = std::string{"资产修订应用失败："} + std::string{applied.message()};
  return result::success;
}

} // namespace gneiss::runtime_internal
