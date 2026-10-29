// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime/loading/action_map_application.hpp"
#include "engine/core/error.hpp"
#include "engine/function/input/input_service.hpp"

namespace gneiss::runtime {
action_map_application::~action_map_application() { cancel(); }
void action_map_application::cancel() noexcept {
  if (request_)
    request_.cancel();
  request_ = {};
  state_ = action_map_apply_state::cancelled;
  result_ = gneiss::engine::error::invalid_state;
}
gneiss::engine::error action_map_application::begin(
    gneiss::engine::asset_request<gneiss::engine::action_map> request) noexcept {
  cancel();
  if (!request)
    return gneiss::engine::error::invalid_argument;
  request_ = std::move(request);
  state_ = action_map_apply_state::loading;
  result_ = gneiss::engine::error::not_ready;
  return gneiss::engine::error::success;
}
gneiss::engine::error action_map_application::poll(gneiss::engine::input_service& target) noexcept {
  if (state_ != action_map_apply_state::loading)
    return result_;
  auto loaded = request_.take();
  if (!loaded && loaded.error().error == gneiss::engine::error::not_ready)
    return gneiss::engine::error::not_ready;
  result_ = loaded ? gneiss::engine::error::success : loaded.error().error;
  if (loaded) {
    try {
      auto map = **loaded;
      result_ = target.replace_action_map(std::move(map));
    } catch (const std::bad_alloc&) {
      result_ = gneiss::engine::error::out_of_memory;
    } catch (...) {
      result_ = gneiss::engine::error::internal;
    }
  }
  request_ = {};
  state_ = result_ == gneiss::engine::error::success ? action_map_apply_state::applied
                                                     : action_map_apply_state::failed;
  return result_;
}
} // namespace gneiss::runtime
