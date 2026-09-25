// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_asset_reloader.h"

#include <gneiss/core/result.hpp>

#include <algorithm>
#include <utility>

namespace gneiss::runtime_internal {

runtime_asset_reloader::runtime_asset_reloader(apply_function apply, begin_function begin,
                                               poll_function poll)
    : apply_(std::move(apply)), begin_(std::move(begin)), poll_(std::move(poll)),
      owner_thread_(std::this_thread::get_id()) {}

result runtime_asset_reloader::execute(const ipc_asset_reload_request& request,
                                       ipc_asset_reload_result& response) noexcept try {
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
  if (pending_) {
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
  auto applied = result::success;
  if (begin_ && poll_ && std::ranges::all_of(request.assets, [](const auto& item) {
        return item.type == ipc_asset_type::texture;
      })) {
    try {
      pending_ = request;
      applied = begin_(request);
      if (applied == result::success) {
        return result::not_ready;
      }
      pending_.reset();
    } catch (...) {
      pending_.reset();
      applied = result::out_of_memory;
    }
  } else {
    applied = apply_(request.assets);
  }
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
} catch (const std::bad_alloc&) {
  return result::out_of_memory;
} catch (...) {
  return result::internal;
}

result runtime_asset_reloader::advance(ipc_asset_reload_result& response, bool& ready) noexcept
    try {
  ready = false;
  if (std::this_thread::get_id() != owner_thread_) {
    return result::invalid_state;
  }
  if (!pending_) {
    return result::success;
  }
  result applied;
  bool completed{};
  const auto operation = poll_(applied, completed);
  if (operation != result::success) {
    return operation;
  }
  if (!completed) {
    return result::success;
  }
  response = {.session_id = pending_->session_id,
              .revision = pending_->revision,
              .status = applied == result::success ? ipc_asset_apply_status::applied
                                                   : ipc_asset_apply_status::failed,
              .message = applied == result::success ? "资产修订已应用" : "异步纹理应用失败"};
  if (applied == result::success) {
    applied_revision_ = pending_->revision;
  }
  pending_.reset();
  ready = true;
  return result::success;
} catch (const std::bad_alloc&) {
  return result::out_of_memory;
} catch (...) {
  return result::internal;
}

} // namespace gneiss::runtime_internal
