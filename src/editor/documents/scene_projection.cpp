// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "documents/scene_projection.hpp"
#include "engine/function/scene/component_adapter.hpp"
namespace gneiss::editor {
namespace {
bool same_transform(const engine::math::transform& a, const engine::math::transform& b) {
  return a.translation == b.translation && a.rotation == b.rotation && a.scale == b.scale;
}
engine::result<bool> update_transforms(engine::scene_instances& scenes, scene_instance_id scene,
                                       const scene_document_snapshot& before,
                                       const scene_document_snapshot& target) {
  if (before.content.get().uuid != target.content.get().uuid || before.assets != target.assets)
    return false;
  auto previous = compose_scene_document(before);
  auto next = compose_scene_document(target);
  if (!previous)
    return previous.error();
  if (!next)
    return next.error();
  if (previous->objects.size() != next->objects.size())
    return false;
  std::map<document_object_address, const composed_document_object*> old;
  for (const auto& object : previous->objects)
    old.emplace(object.address, &object);
  for (const auto& object : next->objects) {
    auto found = old.find(object.address);
    if (found == old.end())
      return false;
    const auto& a = *found->second;
    if (a.parent != object.parent || a.effective.name != object.effective.name ||
        a.prefab_uri != object.prefab_uri ||
        !engine::equal_components(a.effective.components, object.effective.components))
      return false;
    for (const auto& component : a.effective.components)
      if (component->uuid != object.effective.components.identity(component->guid()))
        return false;
  }
  std::map<document_object_address, engine::scene_node_info> nodes;
  auto count = scenes.get_node_count(scene);
  if (!count)
    return count.error();
  for (std::uint64_t i = 0; i < *count; ++i) {
    auto info = scenes.get_node_info(scene, i, engine::node_query_detail::components);
    if (!info)
      return info.error();
    auto address = info->prefab ? document_object_address{info->prefab->instance_uuid,
                                                          info->prefab->source_node_uuid}
                                : document_object_address{info->uuid, {}};
    nodes.emplace(address, *info);
  }
  // 所有目标及基线先检查，再开始更新；不把外部变更当作已同步状态。
  for (const auto& object : next->objects) {
    auto found = nodes.find(object.address);
    if (found == nodes.end() ||
        !same_transform(found->second.local_transform, old.at(object.address)->effective.transform))
      return false;
  }
  for (const auto& object : next->objects) {
    if (same_transform(old.at(object.address)->effective.transform, object.effective.transform))
      continue;
    auto status =
        scenes.set_node_transform(scene, nodes.at(object.address).node, object.effective.transform);
    if (status.failed())
      return status;
  }
  return true;
}
}
engine::error project_scene_document(engine::scene_instances& scenes, scene_instance_id scene,
                                     const scene_document_snapshot* baseline,
                                     const scene_document_snapshot& target) noexcept try {
  if (baseline) {
    auto updated = update_transforms(scenes, scene, *baseline, target);
    if (!updated)
      return updated.error();
    if (*updated)
      return engine::error::success;
  }
  auto inputs = make_scene_document_inputs(target);
  if (!inputs)
    return inputs.error();
  engine::scene_instantiation_data prepared;
  engine::scene_diagnostic diagnostic;
  auto status = engine::prepare_loaded_scene(*inputs, {}, prepared, diagnostic, {});
  if (status.failed())
    return status;
  return scenes.replace_prepared(scene, std::move(prepared), target.assets);
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}
}
