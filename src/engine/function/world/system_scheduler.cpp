// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/world/system_scheduler.hpp"
#include "engine/core/result.hpp"

#include <algorithm>
#include <new>

namespace gneiss::engine::world {

gneiss::engine::core::result system_scheduler::add(system_id id, system_callback callback,
                                                   void* user_data) noexcept {
  if (id == 0U || callback == nullptr ||
      std::ranges::any_of(systems_, [id](const entry& value) { return value.id == id; })) {
    return gneiss::engine::core::result::invalid_argument;
  }
  try {
    systems_.push_back({.id = id, .callback = callback, .user_data = user_data});
    return gneiss::engine::core::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::core::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::core::result::internal;
  }
}

gneiss::engine::core::result system_scheduler::run(world_state& world) noexcept {
  for (const auto& system : systems_) {
    const auto result = system.callback(world, system.user_data);
    if (result != gneiss::engine::core::result::success) {
      return result;
    }
  }
  return gneiss::engine::core::result::success;
}

} // namespace gneiss::engine::world
