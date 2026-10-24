// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime/loading/scene_domain.hpp"
#include "engine/resource/asset_manager.hpp"

namespace gneiss::runtime {

scene_domain::~scene_domain() noexcept = default;

gneiss_result scene_domain::initialize(engine::resource::asset_manager& owner,
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
    auto next_scenes = std::make_unique<engine::scene::scene_instances>(
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
