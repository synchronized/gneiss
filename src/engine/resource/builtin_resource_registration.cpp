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
error builtin_resource_registry(meta::registry_ref& output) noexcept try {
  static std::mutex mutex;
  static meta::registry_ref registry;
  const std::scoped_lock lock(mutex);
  if (!registry) {
    auto created = meta::create_registry();
    if (!created)
      return created.error();
    auto pending = std::move(*created);
    auto status = error::success;
    if (status == error::success)
      status = register_material_reflection(pending);
    if (status == error::success)
      status = register_action_map_reflection(pending);
    if (status == error::success)
      status = register_scene_reflection(pending);
    if (status == error::success)
      status = register_prefab_reflection(pending);
    if (status == error::success)
      status = freeze(pending);
    if (status.failed())
      return status;
    registry = std::move(pending);
  }
  output = registry;
  return error::success;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
}

namespace gneiss::engine {
error register_scene_reflection(const meta::registry_ref& registry) noexcept {
  return meta::type_builder<scene_description>{registry}
      .serialization<&encode_scene, &decode_scene>()
      .commit();
}
error register_prefab_reflection(const meta::registry_ref& registry) noexcept {
  return meta::type_builder<prefab_description>{registry}
      .serialization<&encode_prefab, &decode_prefab>()
      .commit();
}
error register_material_reflection(const meta::registry_ref& registry) noexcept {
  return meta::type_builder<material_description>{registry}
      .serialization<&encode_material, &decode_material>()
      .commit();
}
error register_action_map_reflection(const meta::registry_ref& registry) noexcept {
  return meta::type_builder<action_map>{registry}
      .serialization<nullptr, &decode_action_map>()
      .commit();
}
}
