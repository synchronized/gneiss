// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_WORLD_WORLD_SERVICE_HPP_
#define GNEISS_WORLD_WORLD_SERVICE_HPP_
#include <gneiss/scene.h>
#include <gneiss/world.h>

namespace gneiss::world_internal {
/** 私有同版本宿主接口，不安装；唯一注册表负责线程和 generation 校验。 */
struct camera_settings {
  float vertical_field_of_view_radians{};
  float near_plane{};
  float far_plane{};
};
[[nodiscard]] GNEISS_API gneiss_result create(gneiss_world& out_world) noexcept;
[[nodiscard]] GNEISS_API gneiss_result destroy(gneiss_world world) noexcept;
[[nodiscard]] GNEISS_API gneiss_result entity_create(gneiss_world world,
                                                     gneiss_entity_id& out_entity) noexcept;
[[nodiscard]] GNEISS_API gneiss_result entity_destroy(gneiss_world world,
                                                      gneiss_entity_id entity) noexcept;
[[nodiscard]] GNEISS_API gneiss_result entity_is_alive(gneiss_world world, gneiss_entity_id entity,
                                                       uint8_t& out_is_alive) noexcept;
[[nodiscard]] GNEISS_API gneiss_result entity_count(gneiss_world world,
                                                    uint64_t& out_count) noexcept;
[[nodiscard]] GNEISS_API gneiss_result entity_set_camera(gneiss_world world,
                                                         gneiss_entity_id entity,
                                                         const gneiss_camera& camera) noexcept;
[[nodiscard]] GNEISS_API gneiss_result entity_configure_camera(
    gneiss_world world, gneiss_entity_id entity, const camera_settings& desc) noexcept;
[[nodiscard]] GNEISS_API gneiss_result entity_get_camera(gneiss_world world,
                                                         gneiss_entity_id entity,
                                                         camera_settings& out_camera) noexcept;
[[nodiscard]] GNEISS_API gneiss_result entity_remove_camera(gneiss_world world,
                                                            gneiss_entity_id entity) noexcept;
[[nodiscard]] GNEISS_API gneiss_result set_active_camera(gneiss_world world,
                                                         gneiss_entity_id entity) noexcept;
[[nodiscard]] GNEISS_API gneiss_result get_active_camera(gneiss_world world,
                                                         gneiss_entity_id& out_entity) noexcept;
[[nodiscard]] GNEISS_API gneiss_result entity_set_mesh_renderer(
    gneiss_world world, gneiss_entity_id entity, const gneiss_mesh_renderer& renderer) noexcept;
[[nodiscard]] GNEISS_API gneiss_result
entity_remove_mesh_renderer(gneiss_world world, gneiss_entity_id entity) noexcept;
[[nodiscard]] GNEISS_API gneiss_result node_create(gneiss_world world, gneiss_scene_node_id parent,
                                                   gneiss_entity_id entity,
                                                   gneiss_scene_node_id& out_node) noexcept;
[[nodiscard]] GNEISS_API gneiss_result node_destroy(gneiss_world world,
                                                    gneiss_scene_node_id node) noexcept;
[[nodiscard]] GNEISS_API gneiss_result node_reparent(gneiss_world world, gneiss_scene_node_id node,
                                                     gneiss_scene_node_id parent) noexcept;
[[nodiscard]] GNEISS_API gneiss_result node_set_local_transform(
    gneiss_world world, gneiss_scene_node_id node, const gneiss_transform& transform) noexcept;
[[nodiscard]] GNEISS_API gneiss_result node_get_local_transform(
    gneiss_world world, gneiss_scene_node_id node, gneiss_transform& out_transform) noexcept;
[[nodiscard]] GNEISS_API gneiss_result node_get_world_transform(
    gneiss_world world, gneiss_scene_node_id node, gneiss_transform& out_transform) noexcept;
[[nodiscard]] GNEISS_API gneiss_result node_get_entity(gneiss_world world,
                                                       gneiss_scene_node_id node,
                                                       gneiss_entity_id& out_entity) noexcept;
[[nodiscard]] GNEISS_API gneiss_result node_get_parent(gneiss_world world,
                                                       gneiss_scene_node_id node,
                                                       gneiss_scene_node_id& out_parent) noexcept;
[[nodiscard]] GNEISS_API gneiss_result entity_get_local_transform(
    gneiss_world world, gneiss_entity_id entity, gneiss_transform& out_transform) noexcept;
[[nodiscard]] GNEISS_API gneiss_result entity_set_local_transform(
    gneiss_world world, gneiss_entity_id entity, const gneiss_transform& transform) noexcept;
[[nodiscard]] GNEISS_API gneiss_type_id transform_type_id() noexcept;
[[nodiscard]] GNEISS_API gneiss_type_id camera_type_id() noexcept;
[[nodiscard]] GNEISS_API gneiss_result register_reflection(gneiss_type_registry registry);
} // namespace gneiss::world_internal
#endif
