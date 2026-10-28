// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime/loading/scene_domain.hpp"
#include "engine/core/result.hpp"
#include "engine/core/rid.hpp"

namespace gneiss::runtime {

scene_domain::~scene_domain() noexcept = default;

gneiss::engine::result scene_domain::initialize(gneiss::engine::resource_service& render) noexcept {
  if (world_owner_) {
    return gneiss::engine::result::invalid_state;
  }
  gneiss::engine::world_owner next_world;
  const auto result = gneiss::engine::create_native(next_world);
  if (result != gneiss::engine::result::success) {
    return result;
  }
  try {
    auto next_scenes = std::make_unique<gneiss::engine::scene_instances>(*next_world, render);
    if (!next_scenes->is_valid()) {
      return gneiss::engine::result::out_of_memory;
    }
    world_owner_ = std::move(next_world);
    scenes_ = std::move(next_scenes);
    return gneiss::engine::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

} // namespace gneiss::runtime
