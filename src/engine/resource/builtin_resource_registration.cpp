// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "builtin_resource_registration.hpp"
#include "engine/core/meta/registration.hpp"
#include "engine/resource/input/action_map_codec.hpp"
#include "engine/resource/material/material_codec.hpp"
#include "engine/resource/scene/prefab_codec.hpp"
#include "engine/resource/scene/scene_codec.hpp"
#include <mutex>
namespace gneiss::engine {
result builtin_resource_registry(gneiss::engine::meta::registry_ref& output) noexcept try {
  static std::mutex mutex;
  static gneiss::engine::meta::registry_ref registry;
  const std::scoped_lock lock(mutex);
  if (!registry) {
    gneiss::engine::meta::registry_ref pending;
    auto status = create_registry(pending);
    if (status == result::success)
      status = register_material_reflection(pending);
    if (status == result::success)
      status = register_action_map_reflection(pending);
    if (status == result::success)
      status = register_scene_reflection(pending);
    if (status == result::success)
      status = register_prefab_reflection(pending);
    if (status == result::success)
      status = freeze(pending);
    if (status.failed())
      return status;
    registry = std::move(pending);
  }
  output = registry;
  return result::success;
} catch (const std::bad_alloc&) {
  return result::out_of_memory;
} catch (...) {
  return result::internal;
}
}

namespace gneiss::engine {
result register_scene_reflection(const gneiss::engine::meta::registry_ref& registry) noexcept {
  return gneiss::engine::meta::type_builder<scene_description>{registry}
      .serialization<&encode_scene, &decode_scene>()
      .commit();
}
result register_prefab_reflection(const gneiss::engine::meta::registry_ref& registry) noexcept {
  return gneiss::engine::meta::type_builder<prefab_description>{registry}
      .serialization<&encode_prefab, &decode_prefab>()
      .commit();
}
result register_material_reflection(const gneiss::engine::meta::registry_ref& registry) noexcept {
  return gneiss::engine::meta::type_builder<material_description>{registry}
      .serialization<&encode_material, &decode_material>()
      .commit();
}
result register_action_map_reflection(const gneiss::engine::meta::registry_ref& registry) noexcept {
  return gneiss::engine::meta::type_builder<action_map>{registry}
      .serialization<nullptr, &decode_action_map>()
      .commit();
}
}
