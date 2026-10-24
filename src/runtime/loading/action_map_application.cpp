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
  result_ = gneiss::engine::core::result::invalid_state;
}
gneiss::engine::core::result action_map_application::begin(
    engine::resource::asset_request<engine::resource::action_map_asset> request) noexcept {
  cancel();
  if (!request)
    return gneiss::engine::core::result::invalid_argument;
  request_ = std::move(request);
  state_ = action_map_apply_state::loading;
  result_ = gneiss::engine::core::result::not_ready;
  return gneiss::engine::core::result::success;
}
gneiss::engine::core::result
action_map_application::poll(engine::input::input_service& target) noexcept {
  if (state_ != action_map_apply_state::loading)
    return result_;
  engine::resource::asset_request<engine::resource::action_map_asset>::reference asset;
  const auto loaded = request_ ? request_.take(asset) : gneiss::engine::core::result::invalid_state;
  if (loaded == gneiss::engine::core::result::not_ready)
    return gneiss::engine::core::result::not_ready;
  result_ = loaded;
  if (loaded == gneiss::engine::core::result::success) {
    try {
      auto map = asset->map;
      result_ = target.replace_action_map(std::move(map));
    } catch (const std::bad_alloc&) {
      result_ = gneiss::engine::core::result::out_of_memory;
    } catch (...) {
      result_ = gneiss::engine::core::result::internal;
    }
  }
  request_ = {};
  state_ = result_ == gneiss::engine::core::result::success ? action_map_apply_state::applied
                                                            : action_map_apply_state::failed;
  return result_;
}
} // namespace gneiss::runtime
