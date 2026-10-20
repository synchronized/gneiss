// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "caors_action_map.hpp"
#include "input_service.hpp"

namespace gneiss::input_internal {
action_map_application::~action_map_application() { cancel(); }
void action_map_application::cancel() noexcept {
  if (state_ != action_map_apply_state::loading)
    return;
  (void)request_.cancel();
  request_ = {};
  state_ = action_map_apply_state::cancelled;
  result_ = result::invalid_state;
}
result action_map_application::begin(caors::assets::asset_manager& manager,
                                     caors::resource_guid guid, bool reload) noexcept {
  cancel();
  try {
    request_ = reload ? manager.request_reload<action_map_asset>(guid)
                      : manager.request_load<action_map_asset>(guid);
    state_ = action_map_apply_state::loading;
    result_ = result::not_ready;
    return result::success;
  } catch (const std::bad_alloc&) {
    result_ = result::out_of_memory;
  } catch (...) {
    result_ = result::internal;
  }
  state_ = action_map_apply_state::failed;
  return result_;
}
result action_map_application::poll(input_service& target) noexcept {
  try {
    return poll([&target](action_map map) { return target.replace_action_map(std::move(map)); });
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}
result action_map_application::poll(const std::function<result(action_map)>& apply) noexcept {
  if (state_ != action_map_apply_state::loading || !request_.done())
    return result_;
  if (request_.cancelled()) {
    cancel();
    return result_;
  }
  if (!request_.loaded()) {
    const auto error = request_.error().value();
    result_ = error == caors::errc::not_found          ? result::not_found
              : error == caors::errc::invalid_argument ? result::invalid_argument
              : error == caors::errc::out_of_memory    ? result::out_of_memory
                                                       : result::io;
  } else {
    try {
      const auto asset = request_.try_get();
      // InputService 接收独立副本，不修改 CAORS 缓存中的资源。
      result_ = asset && apply ? apply(asset->map) : result::internal;
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
} // namespace gneiss::input_internal
