// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime/loading/scene_domain.hpp"
#include "engine/core/result.hpp"
#include "engine/resource/asset_manager.hpp"

namespace gneiss::runtime {

scene_domain::~scene_domain() noexcept = default;

gneiss::engine::core::result
scene_domain::initialize(engine::resource::asset_manager& owner,
                         engine::render::render_asset_loader& render) noexcept {
  if (world_owner_) {
    return gneiss::engine::core::result::invalid_state;
  }
  engine::world::world_owner next_world;
  gneiss_world borrowed{};
  const auto result = engine::world::create_native(next_world, borrowed);
  if (result != gneiss::engine::core::result::success) {
    return result;
  }
  try {
    auto next_scenes = std::make_unique<engine::scene::scene_instances>(
        *next_world, borrowed, owner.files(), render, owner.prefab_loader());
    if (!next_scenes->is_valid()) {
      return gneiss::engine::core::result::out_of_memory;
    }
    world_owner_ = std::move(next_world);
    scenes_ = std::move(next_scenes);
    return gneiss::engine::core::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::core::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::core::result::internal;
  }
}

} // namespace gneiss::runtime
