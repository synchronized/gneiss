// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/error.hpp"
#include "engine/function/scene/scene_application.hpp"
#include "engine/function/scene/scene_composition.hpp"
#include "engine/function/scene/scene_instance_state.hpp"
#include "engine/function/scene/scene_instances.hpp"
#include "engine/function/scene/scene_preparation.hpp"
#include "engine/function/scene/scene_types.hpp"
#include "engine/function/world/world_state.hpp"

#include <algorithm>

namespace gneiss::engine {
namespace {
result<scene_instantiation_result>
instantiate_effective(world& world, scene_instances& scenes, guid uuid,
                      const composed_scene& composed,
                      const scene_instances::resource_bindings& assets) {
  entity_id previous_camera;
  const auto camera_status = world.get_active_camera(previous_camera);
  if (camera_status.failed() && camera_status != error::not_ready)
    return camera_status;
  auto created = scenes.create_empty(uuid);
  if (!created)
    return created.error();
  struct creation_guard {
    scene_instances& owner;
    scene_instance_id instance;
    engine::world& world;
    entity_id camera;
    ~creation_guard() {
      if (instance) {
        (void)owner.unload(instance);
        if (camera.get() != 0)
          (void)world.set_active_camera(camera);
      }
    }
  } guard{scenes, *created, world, previous_camera};
  std::map<object_address, scene_object_association> associations;
  auto applied = apply_scene(world, scenes, *created, associations, {}, composed, assets);
  if (!applied)
    return applied.error();
  if (!*applied)
    return error::invalid_state;
  scene_instantiation_result receipt;
  receipt.instance = *created;
  receipt.node_count = associations.size();
  receipt.objects.reserve(associations.size());
  for (const auto& object : composed.objects) {
    const auto& binding = associations.at(object.address);
    receipt.objects.push_back(
        {object.address.object, object.address.source, binding.node, binding.entity});
  }
  guard.instance = {};
  return receipt;
}
} // namespace
result<scene_instantiation_result>
scene_instances::replace_prepared(scene_instance_id instance, scene_instantiation_data prepared,
                                  const resource_bindings& assets) {
  if (!native_world_.is_owner_thread())
    return error::invalid_state;
  if (!instances_.get(instance, resource_type::scene_instance))
    return error::invalid_handle;
  std::map<std::string, const prefab_description*, std::less<>> sources;
  for (const auto& [uri, source] : prepared.prefabs) {
    if (!source.resource)
      return error::invalid_argument;
    sources.emplace(uri, source.resource.get());
  }
  auto composed = compose_scene(prepared.description, sources);
  if (!composed)
    return composed.error();
  auto created =
      instantiate_effective(native_world_, *this, prepared.description.uuid, *composed, assets);
  if (!created)
    return created.error();
  // 插入可能扩容 RID 表，必须重新获取槽位地址。
  auto* target = instances_.get(instance, resource_type::scene_instance);
  auto* replacement = instances_.get(created->instance, resource_type::scene_instance);
  (*target)->storage().swap_scene_members(instance, created->instance);
  std::swap(*target, *replacement);
  (*target)->bind(instance);
  (*replacement)->bind(created->instance);
  const auto retired = created->instance;
  created->instance = instance;
  const auto status = unload(retired);
  if (status.failed())
    return status;
  return std::move(*created);
}
result<scene_instantiation_result>
scene_instances::instantiate_prepared(scene_instantiation_data prepared,
                                      const resource_bindings& assets) {
  if (!native_world_.is_owner_thread())
    return error::invalid_state;
  if (!is_valid()) {
    return engine::error::invalid_argument;
  }
  {
    std::map<std::string, const prefab_description*, std::less<>> sources;
    for (const auto& [uri, source] : prepared.prefabs) {
      if (!source.resource)
        return error::invalid_argument;
      sources.emplace(uri, source.resource.get());
    }
    auto composed = compose_scene(prepared.description, sources);
    if (!composed)
      return composed.error();
    return instantiate_effective(native_world_, *this, prepared.description.uuid, *composed,
                                 assets);
  }
}
} // namespace gneiss::engine
