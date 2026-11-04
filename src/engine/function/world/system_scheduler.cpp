// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/world/system_scheduler.hpp"
#include "engine/core/error.hpp"

#include <algorithm>
#include <new>

namespace gneiss::engine {

engine::error system_scheduler::add(system_id id, system_callback callback,
                                    void* user_data) noexcept {
  if (id == 0U || callback == nullptr ||
      std::ranges::any_of(systems_, [id](const entry& value) { return value.id == id; })) {
    return engine::error::invalid_argument;
  }
  try {
    systems_.push_back({.id = id, .callback = callback, .user_data = user_data});
    return engine::error::success;
  } catch (const std::bad_alloc&) {
    return engine::error::out_of_memory;
  } catch (...) {
    return engine::error::internal;
  }
}

engine::error system_scheduler::run(world_state& world) noexcept {
  for (const auto& system : systems_) {
    const auto result = system.callback(world, system.user_data);
    if (result != engine::error::success) {
      return result;
    }
  }
  return engine::error::success;
}

} // namespace gneiss::engine
