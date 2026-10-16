// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/publication_group.hpp"
#include "engine/function/render/render_snapshot.hpp"
#include "engine/function/world/world_types.hpp"
#include <memory>
#include <span>

namespace gneiss::world_internal {
class world_state;
struct world_factory;
}
namespace gneiss::engine {
/** 原生 World 操作入口；拥有唯一 ECS/场景树状态，全部访问限所属线程。
 * domain 由 World 创建工厂分配；私有实现不向宿主暴露 EnTT。 */
class GNEISS_API world final {
public:
  ~world() noexcept;
  world(const world&) = delete;
  world& operator=(const world&) = delete;
  [[nodiscard]] bool is_owner_thread() const noexcept;
  [[nodiscard]] gneiss_result begin_publication(core::publication& output) noexcept;
  [[nodiscard]] gneiss_result publish(const core::publication& group) noexcept;
  [[nodiscard]] gneiss_result retire_publication(const core::publication& group) noexcept;
  [[nodiscard]] gneiss_result
  replace_publications(std::span<const core::publication> previous,
                       std::span<const core::publication> candidates) noexcept;
  [[nodiscard]] gneiss_result bind_publication(const core::publication& group,
                                               world_internal::staged_object object) noexcept;
  [[nodiscard]] gneiss_result
  create_staged_object(const core::publication& group,
                       const world_internal::staged_object_description& description,
                       world_internal::staged_object& output) noexcept;
  [[nodiscard]] gneiss_result destroy_staged_object(const core::publication& group,
                                                    world_internal::staged_object object) noexcept;
  [[nodiscard]] gneiss_result entity_create(gneiss_entity_id& out_entity) noexcept;
  [[nodiscard]] gneiss_result entity_destroy(gneiss_entity_id entity) noexcept;
  [[nodiscard]] gneiss_result entity_is_alive(gneiss_entity_id entity, bool& out_is_alive) noexcept;
  [[nodiscard]] gneiss_result entity_count(uint64_t& out_count) noexcept;
  [[nodiscard]] gneiss_result entity_set_camera(gneiss_entity_id entity,
                                                const world_internal::camera_settings& camera,
                                                bool primary) noexcept;
  [[nodiscard]] gneiss_result
  entity_configure_camera(gneiss_entity_id entity,
                          const world_internal::camera_settings& desc) noexcept;
  [[nodiscard]] gneiss_result
  entity_get_camera(gneiss_entity_id entity, world_internal::camera_settings& out_camera) noexcept;
  [[nodiscard]] gneiss_result entity_remove_camera(gneiss_entity_id entity) noexcept;
  [[nodiscard]] gneiss_result set_active_camera(gneiss_entity_id entity) noexcept;
  [[nodiscard]] gneiss_result get_active_camera(gneiss_entity_id& out_entity) noexcept;
  [[nodiscard]] gneiss_result replace_mesh_renderers(
      std::span<const world_internal::mesh_renderer_replacement> replacements) noexcept;
  [[nodiscard]] gneiss_result
  entity_set_mesh_renderer(gneiss_entity_id entity,
                           const world_internal::mesh_renderer& renderer) noexcept;
  [[nodiscard]] gneiss_result entity_remove_mesh_renderer(gneiss_entity_id entity) noexcept;
  [[nodiscard]] gneiss_result node_create(gneiss_scene_node_id parent, gneiss_entity_id entity,
                                          gneiss_scene_node_id& out_node) noexcept;
  [[nodiscard]] gneiss_result node_destroy(gneiss_scene_node_id node) noexcept;
  [[nodiscard]] gneiss_result node_reparent(gneiss_scene_node_id node,
                                            gneiss_scene_node_id parent) noexcept;
  [[nodiscard]] gneiss_result node_set_local_transform(gneiss_scene_node_id node,
                                                       const engine::transform& transform) noexcept;
  [[nodiscard]] gneiss_result node_get_local_transform(gneiss_scene_node_id node,
                                                       engine::transform& out_transform) noexcept;
  [[nodiscard]] gneiss_result node_get_world_transform(gneiss_scene_node_id node,
                                                       engine::transform& out_transform) noexcept;
  [[nodiscard]] gneiss_result node_get_entity(gneiss_scene_node_id node,
                                              gneiss_entity_id& out_entity) noexcept;
  [[nodiscard]] gneiss_result node_get_parent(gneiss_scene_node_id node,
                                              gneiss_scene_node_id& out_parent) noexcept;
  [[nodiscard]] gneiss_result entity_get_local_transform(gneiss_entity_id entity,
                                                         engine::transform& out_transform) noexcept;
  [[nodiscard]] gneiss_result
  entity_set_local_transform(gneiss_entity_id entity, const engine::transform& transform) noexcept;
  [[nodiscard]] gneiss_result
  get_render_snapshot(std::uint32_t viewport_width, std::uint32_t viewport_height,
                      render_internal::render_snapshot& out_snapshot,
                      const render_internal::view_camera* camera) noexcept;

private:
  friend struct world_internal::world_factory;
  explicit world(std::uint32_t domain);
  std::unique_ptr<world_internal::world_state> state_;
};
}
