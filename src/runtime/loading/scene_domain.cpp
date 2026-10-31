// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime/loading/scene_domain.hpp"
#include "engine/core/error.hpp"
#include "engine/core/rid.hpp"

namespace gneiss::runtime {

scene_domain::~scene_domain() noexcept = default;

gneiss::engine::error
scene_domain::initialize(gneiss::engine::resource_service& resources) noexcept {
  if (world_owner_) {
    return gneiss::engine::error::invalid_state;
  }
  auto next_world = gneiss::engine::create_native();
  if (!next_world)
    return next_world.error();
  try {
    auto next_scenes = std::make_unique<gneiss::engine::scene_instances>(**next_world, resources);
    if (!next_scenes->is_valid()) {
      return gneiss::engine::error::out_of_memory;
    }
    world_owner_ = std::move(*next_world);
    scenes_ = std::move(next_scenes);
    return gneiss::engine::error::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::error::out_of_memory;
  } catch (...) {
    return gneiss::engine::error::internal;
  }
}

} // namespace gneiss::runtime
