// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/scene/scene_instances.hpp"
#include "engine/function/world/world_state.hpp"

#include <utility>

namespace gneiss::engine {
result<scene_instances*> scene_instances::attach(world& owner) {
  if (!owner.is_owner_thread() || owner.scenes_)
    return error::invalid_state;
  auto candidate = std::unique_ptr<scene_instances>(new scene_instances(owner));
  if (!candidate->is_valid())
    return error::invalid_state;
  owner.scenes_ = std::move(candidate);
  return owner.scenes_.get();
}

scene_instances::scene_instances(world& native_world) noexcept : native_world_(native_world) {}
scene_instances::~scene_instances() noexcept = default;
bool scene_instances::is_valid() const noexcept {
  const auto frozen = meta::is_frozen();
  return native_world_.state_->scene_domain != 0U && frozen && *frozen;
}
result<scene_instance_id> scene_instances::create_empty(guid uuid) {
  if (!native_world_.is_owner_thread())
    return error::invalid_state;
  if (!uuid)
    return error::invalid_argument;
  auto created = native_world_.create_scene();
  if (!created)
    return created.error();
  return created->id();
}
error scene_instances::unload(scene_instance_id id) noexcept {
  return native_world_.unload_scene(id);
}
error scene_instances::validate_editable_node(scene_instance_id id,
                                              scene_node_id node) const noexcept {
  return native_world_.validate_scene_node(id, node);
}
result<std::uint64_t> scene_instances::get_node_count(scene_instance_id id) const {
  return native_world_.scene_size(id);
}
result<scene_node_id> scene_instances::find_node(scene_instance_id id, guid uuid) const {
  return native_world_.find_scene_node(id, uuid);
}
result<scene_node_info> scene_instances::get_node_info(scene_instance_id id, std::uint64_t index,
                                                       node_query_detail detail) const {
  return native_world_.scene_node_info_at(id, index, detail);
}
result<scene_node_id> scene_instances::create_node(scene_instance_id id,
                                                   const node_creation& desc) {
  return native_world_.create_scene_node(id, desc);
}
error scene_instances::reparent_node(scene_instance_id id, scene_node_id node,
                                     scene_node_id parent) {
  return native_world_.reparent_scene_node(id, node, parent);
}
error scene_instances::destroy_subtree(scene_instance_id id, scene_node_id node) {
  return native_world_.destroy_scene_subtree(id, node);
}
error scene_instances::destroy_node(scene_instance_id id, scene_node_id node) {
  return native_world_.destroy_scene_node(id, node);
}
} // namespace gneiss::engine
