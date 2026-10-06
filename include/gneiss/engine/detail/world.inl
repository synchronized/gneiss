// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_DETAIL_WORLD_INL_
#define GNEISS_DETAIL_WORLD_INL_

#include <gneiss/engine/world.hpp>

namespace gneiss {

inline result world_ref::register_reflection(type_registry& registry) noexcept {
  return from_native(gneiss_world_register_reflection(registry.get()));
}

inline result world_ref::create_entity(entity_id& out_entity) const noexcept {
  gneiss_entity_id native_entity = GNEISS_NULL_ENTITY_ID;
  const auto native_result = gneiss_world_entity_create(handle_, &native_entity);
  if (native_result == GNEISS_SUCCESS) {
    out_entity = entity_id{native_entity};
  }
  return from_native(native_result);
}

inline result world_ref::entity_count(std::uint64_t& output) const noexcept {
  return from_native(gneiss_world_entity_count(handle_, &output));
}

inline result world_ref::destroy_entity(entity_id entity) const noexcept {
  return from_native(gneiss_world_entity_destroy(handle_, entity.get()));
}

inline result world_ref::set_camera(entity_id entity, const camera& value) const noexcept {
  const auto native = to_native(value);
  return from_native(gneiss_world_entity_set_camera(handle_, entity.get(), &native));
}

inline result world_ref::configure_camera(entity_id entity,
                                          const camera_desc& value) const noexcept {
  const auto native = to_native(value);
  return from_native(gneiss_world_entity_configure_camera(handle_, entity.get(), &native));
}

inline result world_ref::get_camera(entity_id entity, camera_desc& out_camera) const noexcept {
  gneiss_camera_desc native = GNEISS_CAMERA_DESC_INIT;
  const auto status = from_native(gneiss_world_entity_get_camera(handle_, entity.get(), &native));
  if (status.ok()) {
    out_camera = from_native(native);
  }
  return status;
}

inline result world_ref::remove_camera(entity_id entity) const noexcept {
  return from_native(gneiss_world_entity_remove_camera(handle_, entity.get()));
}

inline result world_ref::set_active_camera(entity_id entity) const noexcept {
  return from_native(gneiss_world_set_active_camera(handle_, entity.get()));
}

inline result world_ref::get_active_camera(entity_id& out_entity) const noexcept {
  gneiss_entity_id native_entity = GNEISS_NULL_ENTITY_ID;
  const auto native_result = gneiss_world_get_active_camera(handle_, &native_entity);
  out_entity = entity_id{native_entity};
  return from_native(native_result);
}

inline result world_ref::set_mesh_renderer(entity_id entity,
                                           const mesh_renderer& value) const noexcept {
  const auto native = to_native(value);
  return from_native(gneiss_world_entity_set_mesh_renderer(handle_, entity.get(), &native));
}

inline result world_ref::remove_mesh_renderer(entity_id entity) const noexcept {
  return from_native(gneiss_world_entity_remove_mesh_renderer(handle_, entity.get()));
}

inline result world_ref::create_scene_node(scene_node_id parent, entity_id entity,
                                           scene_node_id& out_node) const noexcept {
  gneiss_scene_node_id native_node = GNEISS_NULL_SCENE_NODE_ID;
  const auto native_result =
      gneiss_scene_node_create(handle_, parent.get(), entity.get(), &native_node);
  if (native_result == GNEISS_SUCCESS) {
    out_node = scene_node_id{native_node};
  }
  return from_native(native_result);
}

inline result world_ref::destroy_scene_node(scene_node_id node) const noexcept {
  return from_native(gneiss_scene_node_destroy(handle_, node.get()));
}

inline result world_ref::reparent_scene_node(scene_node_id node,
                                             scene_node_id parent) const noexcept {
  return from_native(gneiss_scene_node_reparent(handle_, node.get(), parent.get()));
}

inline result world_ref::set_local_transform(scene_node_id node,
                                             const transform& value) const noexcept {
  const auto native = to_native(value);
  return from_native(gneiss_scene_node_set_local_transform(handle_, node.get(), &native));
}

inline result world_ref::set_local_transform(entity_id entity,
                                             const transform& value) const noexcept {
  const auto native = to_native(value);
  return from_native(gneiss_world_entity_set_local_transform(handle_, entity.get(), &native));
}

inline result world_ref::get_local_transform(scene_node_id node, transform& output) const noexcept {
  gneiss_transform native{};
  const auto status =
      from_native(gneiss_scene_node_get_local_transform(handle_, node.get(), &native));
  if (status.ok()) {
    output = from_native(native);
  }
  return status;
}

inline result world_ref::get_entity(scene_node_id node, entity_id& output) const noexcept {
  gneiss_entity_id value{};
  const auto status = from_native(gneiss_scene_node_get_entity(handle_, node.get(), &value));
  if (status.ok()) {
    output = entity_id{value};
  }
  return status;
}

inline result world_ref::get_parent(scene_node_id node, scene_node_id& output) const noexcept {
  gneiss_scene_node_id value{};
  const auto status = from_native(gneiss_scene_node_get_parent(handle_, node.get(), &value));
  if (status.ok()) {
    output = scene_node_id{value};
  }
  return status;
}

inline result world_ref::get_local_transform(entity_id entity, transform& output) const noexcept {
  gneiss_transform native{};
  const auto status =
      from_native(gneiss_world_entity_get_local_transform(handle_, entity.get(), &native));
  if (status.ok()) {
    output = from_native(native);
  }
  return status;
}

inline result world_ref::get_world_transform(scene_node_id node,
                                             transform& out_transform) const noexcept {
  gneiss_transform native{};
  const auto status =
      from_native(gneiss_scene_node_get_world_transform(handle_, node.get(), &native));
  if (status.ok()) {
    out_transform = from_native(native);
  }
  return status;
}

inline result world_ref::is_alive(entity_id entity, bool& out_is_alive) const noexcept {
  uint8_t native_is_alive = 0;
  const auto native_result = gneiss_world_entity_is_alive(handle_, entity.get(), &native_is_alive);
  if (native_result == GNEISS_SUCCESS) {
    out_is_alive = native_is_alive != 0;
  }
  return from_native(native_result);
}

inline result world::create(world& out_world) noexcept {
  const gneiss_world_desc desc = GNEISS_WORLD_DESC_INIT;
  gneiss_world handle = GNEISS_NULL_WORLD;
  const auto native_result = gneiss_world_create(&desc, &handle);
  if (native_result == GNEISS_SUCCESS) {
    world candidate;
    candidate.handle_ = handle;
    const auto closed = out_world.reset();
    if (closed.failed()) {
      return closed;
    }
    out_world.handle_ = candidate.release();
  }
  return from_native(native_result);
}

inline result world::reset() noexcept {
  if (handle_ == GNEISS_NULL_WORLD) {
    return result::success;
  }
  const auto status = from_native(gneiss_world_destroy(handle_));
  if (status.failed() && status != result::invalid_handle) {
    return status;
  }
  handle_ = GNEISS_NULL_WORLD;
  return result::success;
}

inline void world::reset_or_terminate() noexcept {
  if (reset().failed()) {
    std::terminate();
  }
}

} // namespace gneiss

#endif
