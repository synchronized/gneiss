// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "world/system_scheduler.h"

#include <algorithm>
#include <new>

namespace gneiss::world_internal {

gneiss_result system_scheduler::add(system_id id, system_callback callback,
                                    void* user_data) noexcept {
  if (id == 0U || callback == nullptr ||
      std::ranges::any_of(systems_, [id](const entry& value) { return value.id == id; })) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    systems_.push_back({.id = id, .callback = callback, .user_data = user_data});
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result system_scheduler::run(world_state& world) noexcept {
  for (const auto& system : systems_) {
    const auto result = system.callback(world, system.user_data);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
  }
  return GNEISS_SUCCESS;
}

} // namespace gneiss::world_internal
