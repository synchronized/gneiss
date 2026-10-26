// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/entity_id.hpp"
#include "engine/core/math/transform.hpp"
#include "engine/core/scene_types.hpp"
#include "engine/function/world/world_components.hpp"

namespace gneiss::engine {
struct mesh_renderer_replacement {
  entity_id entity{};
  mesh_renderer expected{};
  mesh_renderer replacement{};
};
}
