// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/function/scene/component_registration.hpp"
namespace gneiss::engine {
/** 未注册适配器的描述不得被静默忽略。 */
inline error validate_scene_components(const game_object_description& source) noexcept {
  if (source.prefab && !source.components.empty())
    return error::invalid_argument;
  return validate_components(source.components);
}
}
