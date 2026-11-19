// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "function/scene/detail/system_scheduler.hpp"
#include <gneiss/engine/core/base/error.hpp>

#include <algorithm>
#include <new>

namespace gneiss {

gneiss::error system_scheduler::add(system_id id, system_callback callback, void* user_data) {
  if (id == 0U || callback == nullptr ||
      std::ranges::any_of(systems_, [id](const entry& value) { return value.id == id; })) {
    return gneiss::error::invalid_argument;
  }
  {
    systems_.push_back({.id = id, .callback = callback, .user_data = user_data});
    return gneiss::error::success;
  }
}

gneiss::error system_scheduler::run(detail::world_component_access::storage& world) noexcept {
  for (const auto& system : systems_) {
    const auto result = system.callback(world, system.user_data);
    if (result != gneiss::error::success) {
      return result;
    }
  }
  return gneiss::error::success;
}

} // namespace gneiss
