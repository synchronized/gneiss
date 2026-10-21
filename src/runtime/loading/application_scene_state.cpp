// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/world/world_service.hpp"

#include "runtime/loading/application_scene_state.hpp"

namespace gneiss::application_internal {

application_scene_state::application_scene_state(engine::asset_manager& owner,
                                                 render_internal::render_asset_loader& render)
    : files_(owner.files()), assets(render), prefabs(owner.prefab_loader()) {}

application_scene_state::~application_scene_state() noexcept {
  scenes.reset();
  // world_owner_ 在场景实例清理后自动撤销借用 RID 并释放真实 World。
}

gneiss_result application_scene_state::initialize() noexcept {
  if (world != GNEISS_NULL_WORLD) {
    return GNEISS_ERROR_INVALID_STATE;
  }
  const auto result = gneiss::world_internal::create_native(world_owner_, world);
  if (result != GNEISS_SUCCESS) {
    return result;
  }
  try {
    scenes = std::make_unique<scene_internal::scene_instance_service>(*world_owner_, world, files_,
                                                                      assets, prefabs);
    return scenes->is_valid() ? GNEISS_SUCCESS : GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

} // namespace gneiss::application_internal
