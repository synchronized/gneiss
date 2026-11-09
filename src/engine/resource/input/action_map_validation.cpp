// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/error.hpp"
#include "engine/resource/input/action_map.hpp"

#include <cmath>

namespace gneiss::engine {
serialization::diagnostic validate_action_map(const action_map_description& map) {
  if (map.actions.empty()) {
    return {
        .path = "/actions", .message = "输入映射必须包含动作", .error = error::invalid_argument};
  }
  for (std::size_t index = 0; index < map.actions.size(); ++index) {
    const auto& action = map.actions[index];
    const auto path = "/actions/" + std::to_string(index);
    if (action.name.empty() || action.bindings.empty()) {
      return {.path = path, .message = "动作必须有名称和绑定", .error = error::invalid_argument};
    }
    for (std::size_t previous = 0; previous < index; ++previous) {
      if (map.actions[previous].name == action.name) {
        return {
            .path = path + "/name", .message = "动作名称重复", .error = error::invalid_argument};
      }
    }
    for (std::size_t binding_index = 0; binding_index < action.bindings.size(); ++binding_index) {
      const auto& binding = action.bindings[binding_index];
      if (binding.physical_key == 0U || binding.physical_key >= 256U ||
          !std::isfinite(binding.scale) || binding.scale == 0.0F || binding.scale < -1.0F ||
          binding.scale > 1.0F) {
        return {.path = path + "/bindings/" + std::to_string(binding_index),
                .message = "按键或 scale 超出有效范围",
                .error = error::invalid_argument};
      }
    }
  }
  return {};
}

} // namespace gneiss::engine
