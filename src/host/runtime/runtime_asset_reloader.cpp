// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_asset_reloader.hpp"

#include "engine/core/error.hpp"

#include <algorithm>
#include <utility>

namespace gneiss::host {

runtime_asset_reloader::runtime_asset_reloader(begin_function begin, poll_function poll)
    : begin_(std::move(begin)), poll_(std::move(poll)), owner_thread_(std::this_thread::get_id()) {}

gneiss::engine::error runtime_asset_reloader::execute(const ipc_asset_reload_request& request,
                                                      ipc_asset_reload_result& response) noexcept
    try {
  response = {.session_id = request.session_id,
              .revision = request.revision,
              .status = ipc_asset_apply_status::failed,
              .message = {}};
  if (request.session_id == 0U || request.revision == 0U || request.assets.empty()) {
    response.message = "资产重载请求无效";
    return gneiss::engine::error::invalid_argument;
  }
  if (std::this_thread::get_id() != owner_thread_) {
    response.message = "资产重载只能在 Runtime 主线程执行";
    return gneiss::engine::error::invalid_state;
  }
  if (pending_) {
    return gneiss::engine::error::invalid_state;
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
    return gneiss::engine::error::success;
  }
  auto applied = gneiss::engine::error::success;
  if (std::ranges::all_of(request.assets,
                          [](const auto& item) {
                            return item.type == ipc_asset_type::texture ||
                                   item.type == ipc_asset_type::material ||
                                   item.type == ipc_asset_type::static_mesh;
                          }) ||
      (request.assets.size() == 1U && (request.assets.front().type == ipc_asset_type::scene ||
                                       request.assets.front().type == ipc_asset_type::prefab))) {
    if (!begin_ || !poll_) {
      response.message = "资产重载需要完整的异步服务";
      return gneiss::engine::error::success;
    }
    try {
      pending_ = request;
      applied = begin_(request);
      if (applied == gneiss::engine::error::success) {
        return gneiss::engine::error::not_ready;
      }
      pending_.reset();
    } catch (...) {
      pending_.reset();
      applied = gneiss::engine::error::out_of_memory;
    }
  } else {
    response.message = "不支持混合场景结构与渲染资产的重载批次";
    return gneiss::engine::error::invalid_argument;
  }
  if (applied == gneiss::engine::error::success) {
    applied_revision_ = request.revision;
    response.status = ipc_asset_apply_status::applied;
    response.message = "资产修订已应用";
    return gneiss::engine::error::success;
  }
  if (applied == gneiss::engine::error::unsupported) {
    response.status = ipc_asset_apply_status::restart_required;
    response.message = "该资产修订需要重启 Runtime";
    return gneiss::engine::error::success;
  }
  response.message = std::string{"资产修订应用失败："} + std::string{applied.message()};
  return gneiss::engine::error::success;
} catch (const std::bad_alloc&) {
  return gneiss::engine::error::out_of_memory;
} catch (...) {
  return gneiss::engine::error::internal;
}

gneiss::engine::error runtime_asset_reloader::advance(ipc_asset_reload_result& response,
                                                      bool& ready) noexcept try {
  ready = false;
  if (std::this_thread::get_id() != owner_thread_) {
    return gneiss::engine::error::invalid_state;
  }
  if (!pending_) {
    return gneiss::engine::error::success;
  }
  gneiss::engine::error applied;
  bool completed{};
  std::vector<ipc_asset_item_result> items;
  auto operation = gneiss::engine::error{gneiss::engine::error::success};
  try {
    operation = poll_(applied, items, completed);
  } catch (const std::bad_alloc&) {
    operation = gneiss::engine::error::out_of_memory;
  } catch (...) {
    operation = gneiss::engine::error::internal;
  }
  if (operation == gneiss::engine::error::not_ready)
    return gneiss::engine::error::success;
  if (operation.failed()) {
    applied = operation;
    completed = true;
  }
  if (!completed) {
    return gneiss::engine::error::success;
  }
  response = {.session_id = pending_->session_id,
              .revision = pending_->revision,
              .status = applied == gneiss::engine::error::success ? ipc_asset_apply_status::applied
                        : applied == gneiss::engine::error::unsupported
                            ? ipc_asset_apply_status::restart_required
                            : ipc_asset_apply_status::failed,
              .message = applied == gneiss::engine::error::success ? "资产修订已应用"
                                                                   : "异步资产应用失败"};
  response.items = std::move(items);
  response.total_assets = static_cast<std::uint32_t>(response.items.size());
  response.completed_assets = static_cast<std::uint32_t>(
      std::ranges::count_if(response.items, [](const auto& item) { return item.applied; }));
  if (applied != gneiss::engine::error::success && response.completed_assets != 0U) {
    response.message = "部分资源已更新，失败项保留原内容";
  }
  if (applied == gneiss::engine::error::success) {
    applied_revision_ = pending_->revision;
  }
  pending_.reset();
  ready = true;
  return gneiss::engine::error::success;
} catch (const std::bad_alloc&) {
  return gneiss::engine::error::out_of_memory;
} catch (...) {
  return gneiss::engine::error::internal;
}

} // namespace gneiss::host
