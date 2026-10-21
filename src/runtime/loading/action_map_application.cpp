// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime/loading/action_map_application.hpp"
#include "engine/function/input/input_service.hpp"

namespace gneiss::runtime {
action_map_application::~action_map_application() { cancel(); }
void action_map_application::cancel() noexcept {
  if (request_)
    request_.cancel();
  request_ = {};
  state_ = action_map_apply_state::cancelled;
  result_ = result::invalid_state;
}
result action_map_application::begin(
    engine::resource::asset_request<engine::resource::action_map_asset> request) noexcept {
  cancel();
  if (!request)
    return result::invalid_argument;
  request_ = std::move(request);
  state_ = action_map_apply_state::loading;
  result_ = result::not_ready;
  return result::success;
}
result action_map_application::poll(engine::input::input_service& target) noexcept {
  if (state_ != action_map_apply_state::loading)
    return result_;
  engine::resource::asset_request<engine::resource::action_map_asset>::reference asset;
  const auto loaded = request_ ? request_.take(asset) : result::invalid_state;
  if (loaded == result::not_ready)
    return result::not_ready;
  result_ = loaded;
  if (loaded == result::success) {
    try {
      auto map = asset->map;
      result_ = target.replace_action_map(std::move(map));
    } catch (const std::bad_alloc&) {
      result_ = result::out_of_memory;
    } catch (...) {
      result_ = result::internal;
    }
  }
  request_ = {};
  state_ =
      result_ == result::success ? action_map_apply_state::applied : action_map_apply_state::failed;
  return result_;
}
} // namespace gneiss::runtime
