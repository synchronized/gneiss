// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/world/world_service.hpp"

#include <gneiss/scene.h>
#include <gneiss/world.h>

#include <new>

extern "C" gneiss_result gneiss_world_create(const gneiss_world_desc* desc,
                                             gneiss_world* out_world) {
  if (out_world == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_world = GNEISS_NULL_WORLD;
  if (desc == nullptr || desc->struct_size < sizeof(gneiss_world_desc) || desc->reserved != 0U) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }

  return gneiss::world_internal::create(*out_world);
}

extern "C" gneiss_result gneiss_world_destroy(gneiss_world world) {
  return gneiss::world_internal::destroy(world);
}

extern "C" gneiss_result gneiss_world_entity_create(gneiss_world world,
                                                    gneiss_entity_id* out_entity) {
  if (out_entity == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_entity = GNEISS_NULL_ENTITY_ID;
  return gneiss::world_internal::entity_create(world, *out_entity);
}

extern "C" gneiss_result gneiss_world_entity_destroy(gneiss_world world, gneiss_entity_id entity) {
  return gneiss::world_internal::entity_destroy(world, entity);
}

extern "C" gneiss_result gneiss_world_entity_is_alive(gneiss_world world, gneiss_entity_id entity,
                                                      uint8_t* out_is_alive) {
  if (out_is_alive == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_is_alive = 0;
  return gneiss::world_internal::entity_is_alive(world, entity, *out_is_alive);
}

extern "C" gneiss_result gneiss_world_entity_count(gneiss_world world, uint64_t* out_count) {
  if (out_count == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_count = 0;
  return gneiss::world_internal::entity_count(world, *out_count);
}

extern "C" gneiss_result gneiss_world_entity_set_camera(gneiss_world world, gneiss_entity_id entity,
                                                        const gneiss_camera* camera) {
  if (camera == nullptr || camera->is_primary > UINT8_C(1) || camera->reserved[0] != 0U ||
      camera->reserved[1] != 0U || camera->reserved[2] != 0U) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  return gneiss::world_internal::entity_set_camera(world, entity, *camera);
}

extern "C" gneiss_result gneiss_world_entity_configure_camera(gneiss_world world,
                                                              gneiss_entity_id entity,
                                                              const gneiss_camera_desc* desc) {
  if (desc == nullptr || desc->struct_size < sizeof(gneiss_camera_desc) || desc->reserved != 0U) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  return gneiss::world_internal::entity_configure_camera(
      world, entity,
      {.vertical_field_of_view_radians = desc->vertical_field_of_view_radians,
       .near_plane = desc->near_plane,
       .far_plane = desc->far_plane});
}

extern "C" gneiss_result gneiss_world_entity_get_camera(gneiss_world world, gneiss_entity_id entity,
                                                        gneiss_camera_desc* out_camera) {
  if (out_camera == nullptr || out_camera->struct_size < sizeof(gneiss_camera_desc)) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  gneiss::world_internal::camera_settings value;
  const auto result = gneiss::world_internal::entity_get_camera(world, entity, value);
  if (result == GNEISS_SUCCESS) {
    *out_camera = GNEISS_CAMERA_DESC_INIT;
    out_camera->vertical_field_of_view_radians = value.vertical_field_of_view_radians;
    out_camera->near_plane = value.near_plane;
    out_camera->far_plane = value.far_plane;
  }
  return result;
}

extern "C" gneiss_result gneiss_world_entity_remove_camera(gneiss_world world,
                                                           gneiss_entity_id entity) {
  return gneiss::world_internal::entity_remove_camera(world, entity);
}

extern "C" gneiss_result gneiss_world_set_active_camera(gneiss_world world,
                                                        gneiss_entity_id entity) {
  return gneiss::world_internal::set_active_camera(world, entity);
}

extern "C" gneiss_result gneiss_world_get_active_camera(gneiss_world world,
                                                        gneiss_entity_id* out_entity) {
  if (out_entity == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_entity = GNEISS_NULL_ENTITY_ID;
  return gneiss::world_internal::get_active_camera(world, *out_entity);
}

extern "C" gneiss_result
gneiss_world_entity_set_mesh_renderer(gneiss_world world, gneiss_entity_id entity,
                                      const gneiss_mesh_renderer* renderer) {
  if (renderer == nullptr || renderer->mesh == GNEISS_NULL_MESH ||
      renderer->material == GNEISS_NULL_MATERIAL) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  return gneiss::world_internal::entity_set_mesh_renderer(world, entity, *renderer);
}

extern "C" gneiss_result gneiss_world_entity_remove_mesh_renderer(gneiss_world world,
                                                                  gneiss_entity_id entity) {
  return gneiss::world_internal::entity_remove_mesh_renderer(world, entity);
}

extern "C" gneiss_result gneiss_scene_node_create(gneiss_world world, gneiss_scene_node_id parent,
                                                  gneiss_entity_id entity,
                                                  gneiss_scene_node_id* out_node) {
  if (out_node == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_node = GNEISS_NULL_SCENE_NODE_ID;
  return gneiss::world_internal::node_create(world, parent, entity, *out_node);
}

extern "C" gneiss_result gneiss_scene_node_destroy(gneiss_world world, gneiss_scene_node_id node) {
  return gneiss::world_internal::node_destroy(world, node);
}

extern "C" gneiss_result gneiss_scene_node_reparent(gneiss_world world, gneiss_scene_node_id node,
                                                    gneiss_scene_node_id parent) {
  return gneiss::world_internal::node_reparent(world, node, parent);
}

extern "C" gneiss_result gneiss_scene_node_set_local_transform(gneiss_world world,
                                                               gneiss_scene_node_id node,
                                                               const gneiss_transform* transform) {
  if (transform == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  return gneiss::world_internal::node_set_local_transform(world, node, *transform);
}

extern "C" gneiss_result gneiss_scene_node_get_local_transform(gneiss_world world,
                                                               gneiss_scene_node_id node,
                                                               gneiss_transform* out_transform) {
  if (out_transform == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  return gneiss::world_internal::node_get_local_transform(world, node, *out_transform);
}

extern "C" gneiss_result gneiss_scene_node_get_world_transform(gneiss_world world,
                                                               gneiss_scene_node_id node,
                                                               gneiss_transform* out_transform) {
  if (out_transform == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  return gneiss::world_internal::node_get_world_transform(world, node, *out_transform);
}

extern "C" gneiss_result gneiss_scene_node_get_entity(gneiss_world world, gneiss_scene_node_id node,
                                                      gneiss_entity_id* out_entity) {
  if (out_entity == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_entity = GNEISS_NULL_ENTITY_ID;
  return gneiss::world_internal::node_get_entity(world, node, *out_entity);
}

extern "C" gneiss_result gneiss_scene_node_get_parent(gneiss_world world, gneiss_scene_node_id node,
                                                      gneiss_scene_node_id* out_parent) {
  if (out_parent == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_parent = GNEISS_NULL_SCENE_NODE_ID;
  return gneiss::world_internal::node_get_parent(world, node, *out_parent);
}

extern "C" gneiss_result gneiss_world_entity_get_local_transform(gneiss_world world,
                                                                 gneiss_entity_id entity,
                                                                 gneiss_transform* out_transform) {
  if (out_transform == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_transform = GNEISS_TRANSFORM_IDENTITY;
  return gneiss::world_internal::entity_get_local_transform(world, entity, *out_transform);
}

extern "C" gneiss_result
gneiss_world_entity_set_local_transform(gneiss_world world, gneiss_entity_id entity,
                                        const gneiss_transform* transform) {
  if (transform == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  return gneiss::world_internal::entity_set_local_transform(world, entity, *transform);
}

extern "C" gneiss_type_id gneiss_transform_type_id(void) {
  return gneiss::world_internal::transform_type_id();
}
extern "C" gneiss_type_id gneiss_camera_type_id(void) {
  return gneiss::world_internal::camera_type_id();
}
extern "C" gneiss_result gneiss_world_register_reflection(gneiss_type_registry registry) {
  try {
    return gneiss::world_internal::register_reflection(registry);
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}
