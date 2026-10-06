// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/world/world_service.hpp"

#include "engine/function/application/application_scene_state.hpp"

namespace gneiss::application_internal {

application_scene_state::application_scene_state(
    const asset_internal::virtual_file_system& files,
    render_internal::render_resource_service& resources)
    : files_(files), assets(files_, cache, resources), prefabs(files_, cache) {}

application_scene_state::~application_scene_state() noexcept {
  scenes.reset();
  if (world != GNEISS_NULL_WORLD) {
    (void)gneiss::world_internal::destroy(world);
  }
}

gneiss_result application_scene_state::initialize() noexcept {
  if (world != GNEISS_NULL_WORLD) {
    return GNEISS_ERROR_INVALID_STATE;
  }
  const auto result = gneiss::world_internal::create(world);
  if (result != GNEISS_SUCCESS) {
    return result;
  }
  try {
    scenes =
        std::make_unique<scene_internal::scene_instance_service>(world, files_, assets, prefabs);
    return scenes->is_valid() ? GNEISS_SUCCESS : GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

} // namespace gneiss::application_internal
