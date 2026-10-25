// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime/loading/action_map_application.hpp"
#include "engine/core/result.hpp"
#include "engine/function/input/input_service.hpp"

namespace gneiss::runtime {
action_map_application::~action_map_application() { cancel(); }
void action_map_application::cancel() noexcept {
  if (request_)
    request_.cancel();
  request_ = {};
  state_ = action_map_apply_state::cancelled;
  result_ = gneiss::engine::result::invalid_state;
}
gneiss::engine::result action_map_application::begin(
    gneiss::engine::asset_request<gneiss::engine::action_map> request) noexcept {
  cancel();
  if (!request)
    return gneiss::engine::result::invalid_argument;
  request_ = std::move(request);
  state_ = action_map_apply_state::loading;
  result_ = gneiss::engine::result::not_ready;
  return gneiss::engine::result::success;
}
gneiss::engine::result
action_map_application::poll(gneiss::engine::input_service& target) noexcept {
  if (state_ != action_map_apply_state::loading)
    return result_;
  gneiss::engine::asset_request<gneiss::engine::action_map>::reference asset;
  const auto loaded = request_ ? request_.take(asset) : gneiss::engine::result::invalid_state;
  if (loaded == gneiss::engine::result::not_ready)
    return gneiss::engine::result::not_ready;
  result_ = loaded;
  if (loaded == gneiss::engine::result::success) {
    try {
      auto map = *asset;
      result_ = target.replace_action_map(std::move(map));
    } catch (const std::bad_alloc&) {
      result_ = gneiss::engine::result::out_of_memory;
    } catch (...) {
      result_ = gneiss::engine::result::internal;
    }
  }
  request_ = {};
  state_ = result_ == gneiss::engine::result::success ? action_map_apply_state::applied
                                                      : action_map_apply_state::failed;
  return result_;
}
} // namespace gneiss::runtime
