// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/error.hpp"
#include "engine/core/fatal.hpp"
#include "engine/function/scene/scene_instances.hpp"
#include "engine/function/scene/scene_types.hpp"

#include "engine/core/identifier_conversion.hpp"
#include "engine/function/world/world.hpp"
#include "engine/function/world/world_component_access.hpp"
#include "engine/function/world/world_state.hpp"

#include "engine/function/scene/scene_object.hpp"

#include <algorithm>
#include <cstdint>
#include <ranges>

namespace gneiss::engine {
world_state& scene_instances::storage() const noexcept {
  auto state = detail::world_component_access::get(native_world_);
  if (!state)
    fatal(fatal_reason::unhandled_exception);
  return **state;
}
std::vector<scene_node_id> scene_instances::members(scene_instance_id instance) const {
  std::vector<scene_node_id> result;
  storage().each_scene_member(instance, [&](scene_node_id node) { result.push_back(node); });
  std::ranges::sort(result, {}, [](scene_node_id node) { return node.get(); });
  return result;
}
result<std::uint64_t> scene_instances::get_node_count(scene_instance_id instance) const {
  const auto status = validate(instance);
  if (status.failed())
    return status;
  std::uint64_t count{};
  storage().each_scene_member(instance, [&](scene_node_id) { ++count; });
  return count;
}
error scene_instances::validate_editable_node(scene_instance_id instance,
                                              scene_node_id node) const noexcept {
  const auto status = validate(instance);
  if (status.failed())
    return status;
  return storage().scene_of(node) == instance ? error::success : error::invalid_handle;
}
result<scene_node_id> scene_instances::find_node(scene_instance_id instance, guid uuid) const {
  const auto status = validate(instance);
  if (status.failed())
    return status;
  scene_node_id found;
  storage().each_scene_member(instance, [&](scene_node_id node) {
    if (scene_object_identity(native_world_, node).uuid == uuid)
      found = node;
  });
  if (!found)
    return error::not_found;
  return found;
}

result<scene_node_info> scene_instances::get_node_info(scene_instance_id instance,
                                                       std::uint64_t index,
                                                       node_query_detail detail) const {
  const auto status = validate(instance);
  if (status.failed())
    return status;
  const auto nodes = members(instance);
  if (index >= nodes.size()) {
    return engine::error::not_found;
  }
  const auto node = nodes[static_cast<std::size_t>(index)];
  std::uint64_t entity = UINT64_C(0);
  auto result = engine::write_identifier<entity_id>(entity, [&](auto& native_output) {
    return native_world_.node_get_entity(gneiss::scene_node_id{node}, native_output);
  });
  if (result != engine::error::success || entity == UINT64_C(0)) {
    return result == engine::error::success ? engine::error::invalid_handle : result;
  }
  scene_node_id parent = null_scene_node_id;
  result = native_world_.node_get_parent(gneiss::scene_node_id{node}, parent);
  if (result != engine::error::success) {
    return result;
  }
  scene_node_info out_info{};
  out_info.node = scene_node_id{node};
  out_info.parent = scene_node_id{parent};
  out_info.entity = entity_id{entity};
  out_info.uuid = scene_object_identity(native_world_, node).uuid;
  out_info.name = scene_object_identity(native_world_, node).name;
  if (detail != node_query_detail::identity) {
    auto components = capture_components(native_world_, node);
    if (!components)
      return components.error();
    out_info.components = std::move(*components);
  }
  if (detail == node_query_detail::components) {
    result = native_world_.node_get_local_transform(node, out_info.local_transform);
    if (result.failed())
      return result;
  }
  return out_info;
}

result<scene_node_id> scene_instances::create_node(scene_instance_id instance,
                                                   const node_creation& desc) {
  const auto status = validate(instance);
  if (status.failed())
    return status;
  if (!desc.uuid || find_node(instance, desc.uuid))
    return error::invalid_argument;
  if (desc.parent && validate_editable_node(instance, desc.parent).failed())
    return error::invalid_handle;
  auto created = native_world_.object_create(desc.parent, {desc.uuid, std::string(desc.name)},
                                             desc.local_transform);
  if (!created)
    return created.error();
  try {
    storage().assign_scene(*created, instance);
  } catch (...) {
    (void)native_world_.object_destroy(*created);
    throw;
  }
  return *created;
}

engine::error scene_instances::reparent_node(scene_instance_id instance, scene_node_id node,
                                             scene_node_id parent) {
  const auto status = validate(instance);
  if (status.failed())
    return status;
  if (validate_editable_node(instance, node).failed()) {
    return engine::error::invalid_handle;
  }
  if (parent && validate_editable_node(instance, parent).failed())
    return error::invalid_handle;
  return native_world_.node_reparent(node, parent);
}

engine::error scene_instances::destroy_subtree(scene_instance_id instance, scene_node_id root) {
  const auto status = validate(instance);
  if (status.failed())
    return status;
  if (validate_editable_node(instance, root).failed())
    return error::invalid_handle;
  return native_world_.object_destroy(root);
}

engine::error scene_instances::destroy_node(scene_instance_id instance, scene_node_id node) {
  const auto status = validate(instance);
  if (status.failed())
    return status;
  if (validate_editable_node(instance, node).failed())
    return error::invalid_handle;
  const auto* children = storage().scene().children(node);
  if (children && !children->empty())
    return error::invalid_state;
  return destroy_subtree(instance, node);
}

} // namespace gneiss::engine
