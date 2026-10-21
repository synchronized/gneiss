// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime/loading/application_scene_state.hpp"
#include "engine/resource/asset_manager.hpp"

namespace gneiss::runtime {

application_scene_state::~application_scene_state() noexcept = default;

gneiss_result
application_scene_state::initialize(engine::resource::asset_manager& owner,
                                    engine::render::render_asset_loader& render) noexcept {
  if (world_owner_) {
    return GNEISS_ERROR_INVALID_STATE;
  }
  engine::world::world_owner next_world;
  gneiss_world borrowed{};
  const auto result = engine::world::create_native(next_world, borrowed);
  if (result != GNEISS_SUCCESS) {
    return result;
  }
  try {
    auto next_scenes = std::make_unique<engine::scene::scene_instance_service>(
        *next_world, borrowed, owner.files(), render, owner.prefab_loader());
    if (!next_scenes->is_valid()) {
      return GNEISS_ERROR_OUT_OF_MEMORY;
    }
    world_owner_ = std::move(next_world);
    scenes_ = std::move(next_scenes);
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

} // namespace gneiss::runtime
