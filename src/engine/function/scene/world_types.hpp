// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/entity_id.hpp"
#include "engine/core/math/transform.hpp"
#include "engine/function/scene/components.hpp"
#include "engine/function/scene/scene_types.hpp"

namespace gneiss::engine {
struct mesh_renderer_replacement {
  entity_id entity{};
  mesh_renderer expected{};
  mesh_renderer replacement{};
};
}
