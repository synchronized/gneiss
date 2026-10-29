// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/function/meta_registration.hpp"
#include "engine/function/world/world_reflection.hpp"
#include "engine/resource/builtin_resource_registration.hpp"
#include "engine/resource/scene/scene_codec.hpp"
#include <mutex>
#include <new>
namespace gneiss::engine {
error initialize_meta(meta_registration_callback callback) noexcept try {
  static std::mutex mutex;
  static bool attempted = false;
  static error status = error::not_ready;
  const std::scoped_lock lock(mutex);
  if (attempted)
    return callback ? error::invalid_state : status;
  attempted = true;
  // 包含 callback 异常在内的失败必须留下明确终态，不允许后续调用误报成功。
  status = error::internal;
  try {
    status = register_reflection();
    if (status.ok())
      status = register_component_descriptions();
    if (status.ok())
      status = register_material_reflection();
    if (status.ok())
      status = register_action_map_reflection();
    if (status.ok())
      status = register_scene_reflection();
    if (status.ok())
      status = register_prefab_reflection();
    if (status.ok() && callback)
      status = callback();
    if (status.ok())
      status = meta::freeze();
  } catch (const std::bad_alloc&) {
    status = error::out_of_memory;
  } catch (...) {
    status = error::internal;
  }
  return status;
} catch (...) {
  return error::internal;
}
}
