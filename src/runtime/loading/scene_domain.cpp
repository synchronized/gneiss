// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime/loading/scene_domain.hpp"
#include "engine/core/error.hpp"
#include "engine/core/rid.hpp"

namespace gneiss::runtime {

scene_domain::~scene_domain() noexcept = default;

gneiss::engine::error scene_domain::initialize(gneiss::engine::asset_manager& resources) {
  if (world_owner_) {
    return gneiss::engine::error::invalid_state;
  }
  auto next_world = gneiss::engine::create_native();
  if (!next_world)
    return next_world.error();
  {
    auto next_scenes = gneiss::engine::scene_instances::attach(**next_world, resources);
    if (!next_scenes) {
      return next_scenes.error();
    }
    world_owner_ = std::move(*next_world);
    return gneiss::engine::error::success;
  }
}

} // namespace gneiss::runtime
