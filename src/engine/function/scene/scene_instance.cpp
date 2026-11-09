// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/error.hpp"
#include "engine/core/fatal.hpp"
#include "engine/function/scene/scene_instance_state.hpp"
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
scene_instance_state::scene_instance_state(world& native_world) noexcept
    : native_world_(native_world) {}

scene_instance_state::~scene_instance_state() noexcept { rollback(); }

world_state& scene_instance_state::storage() const noexcept {
  auto state = detail::world_component_access::get(native_world_);
  if (!state)
    fatal(fatal_reason::unhandled_exception);
  return **state;
}
std::vector<scene_node_id> scene_instance_state::members() const {
  std::vector<scene_node_id> result;
  storage().each_scene_member(id_, [&](scene_node_id node) { result.push_back(node); });
  std::ranges::sort(result, {}, [](scene_node_id node) { return node.get(); });
  return result;
}
std::uint64_t scene_instance_state::node_count() const noexcept {
  std::uint64_t count{};
  storage().each_scene_member(id_, [&](scene_node_id) { ++count; });
  return count;
}
bool scene_instance_state::contains_editable_node(scene_node_id node) const noexcept {
  return id_ && storage().scene_of(node) == id_;
}
void scene_instance_state::adopt(scene_node_id node) {
  try {
    storage().assign_scene(node, id_);
  } catch (...) {
    (void)native_world_.object_destroy(node);
    throw;
  }
}
void scene_instance_state::rollback() noexcept try {
  storage().destroy_scene_members(id_);
} catch (...) {
  gneiss::engine::fatal_current_exception();
}
scene_node_id scene_instance_state::find_node(guid uuid) const {
  scene_node_id found;
  storage().each_scene_member(id_, [&](scene_node_id node) {
    if (scene_object_identity(native_world_, node).uuid == uuid)
      found = node;
  });
  return found;
}

result<scene_node_info> scene_instance_state::get_node_info(std::uint64_t index,
                                                            node_query_detail detail) const {
  const auto nodes = members();
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

result<scene_node_id> scene_instance_state::create_node(const node_creation& desc) {
  if (!desc.uuid || find_node(desc.uuid))
    return error::invalid_argument;
  if (desc.parent && !contains_editable_node(desc.parent))
    return error::invalid_handle;
  auto created = native_world_.object_create(desc.parent, {desc.uuid, std::string(desc.name)},
                                             desc.local_transform);
  if (!created)
    return created.error();
  adopt(*created);
  return *created;
}

engine::error scene_instance_state::reparent_node(scene_node_id node, scene_node_id parent) {
  if (!contains_editable_node(node)) {
    return engine::error::invalid_handle;
  }
  if (parent && !contains_editable_node(parent))
    return error::invalid_handle;
  return native_world_.node_reparent(node, parent);
}

engine::error scene_instance_state::destroy_subtree(scene_node_id root) {
  if (!contains_editable_node(root))
    return error::invalid_handle;
  return native_world_.object_destroy(root);
}

engine::error scene_instance_state::destroy_node(scene_node_id node) {
  if (!contains_editable_node(node))
    return error::invalid_handle;
  const auto* children = storage().scene().children(node);
  if (children && !children->empty())
    return error::invalid_state;
  return destroy_subtree(node);
}

} // namespace gneiss::engine
