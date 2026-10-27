// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "engine/resource/input/action_map.hpp"

#include <cmath>

namespace gneiss::engine {
gneiss::engine::result validate_action_map(const action_map& map) noexcept {
  if (map.actions.empty()) {
    return gneiss::engine::result::invalid_argument;
  }
  for (std::size_t index = 0; index < map.actions.size(); ++index) {
    const auto& action = map.actions[index];
    if (action.name.empty() || action.bindings.empty()) {
      return gneiss::engine::result::invalid_argument;
    }
    for (std::size_t previous = 0; previous < index; ++previous) {
      if (map.actions[previous].name == action.name) {
        return gneiss::engine::result::invalid_argument;
      }
    }
    for (const auto& binding : action.bindings) {
      if (binding.physical_key == 0U || binding.physical_key >= 256U ||
          !std::isfinite(binding.scale) || binding.scale == 0.0F || binding.scale < -1.0F ||
          binding.scale > 1.0F) {
        return gneiss::engine::result::invalid_argument;
      }
    }
  }
  return gneiss::engine::result::success;
}

} // namespace gneiss::engine
