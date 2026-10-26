// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "resource_registry.hpp"
#include "engine/core/meta/type_registration.hpp"
#include "engine/resource/loaders/action_map_serialization.hpp"
#include "engine/resource/loaders/material_serialization.hpp"
#include "engine/resource/loaders/scene_serialization.hpp"
#include <mutex>
namespace gneiss::engine {
result builtin_resource_registry(registry_ref& output) noexcept try {
  static std::mutex mutex;
  static registry_ref registry;
  const std::scoped_lock lock(mutex);
  if (!registry) {
    registry_ref pending;
    auto status = create_native(pending);
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
