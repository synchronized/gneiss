// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/publication_group.hpp"
#include "engine/function/render/render_snapshot.hpp"
#include "engine/function/world/world_types.hpp"
#include <gneiss/engine/core/entity.hpp>
#include <gneiss/engine/core/scene_types.hpp>
#include <memory>
#include <span>

namespace gneiss::engine::world {
class world_state;
struct world_factory;
}
namespace gneiss::engine::world {
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
                                               engine::world::staged_object object) noexcept;
  [[nodiscard]] gneiss_result
  create_staged_object(const core::publication& group,
                       const engine::world::staged_object_description& description,
                       engine::world::staged_object& output) noexcept;
  [[nodiscard]] gneiss_result destroy_staged_object(const core::publication& group,
                                                    engine::world::staged_object object) noexcept;
  [[nodiscard]] gneiss_result entity_create(entity_id& out_entity) noexcept;
  [[nodiscard]] gneiss_result entity_destroy(entity_id entity) noexcept;
  [[nodiscard]] gneiss_result entity_is_alive(entity_id entity, bool& out_is_alive) noexcept;
  [[nodiscard]] gneiss_result entity_count(uint64_t& out_count) noexcept;
  [[nodiscard]] gneiss_result entity_set_camera(entity_id entity,
                                                const engine::world::camera_settings& camera,
                                                bool primary) noexcept;
  [[nodiscard]] gneiss_result
  entity_configure_camera(entity_id entity, const engine::world::camera_settings& desc) noexcept;
  [[nodiscard]] gneiss_result
  entity_get_camera(entity_id entity, engine::world::camera_settings& out_camera) noexcept;
  [[nodiscard]] gneiss_result entity_remove_camera(entity_id entity) noexcept;
  [[nodiscard]] gneiss_result set_active_camera(entity_id entity) noexcept;
  [[nodiscard]] gneiss_result get_active_camera(entity_id& out_entity) noexcept;
  [[nodiscard]] gneiss_result replace_mesh_renderers(
      std::span<const engine::world::mesh_renderer_replacement> replacements) noexcept;
  [[nodiscard]] gneiss_result
  entity_set_mesh_renderer(entity_id entity, const engine::world::mesh_renderer& renderer) noexcept;
  [[nodiscard]] gneiss_result entity_remove_mesh_renderer(entity_id entity) noexcept;
  [[nodiscard]] gneiss_result node_create(scene_node_id parent, entity_id entity,
                                          scene_node_id& out_node) noexcept;
  [[nodiscard]] gneiss_result node_destroy(scene_node_id node) noexcept;
  [[nodiscard]] gneiss_result node_reparent(scene_node_id node, scene_node_id parent) noexcept;
  [[nodiscard]] gneiss_result node_set_local_transform(scene_node_id node,
                                                       const engine::transform& transform) noexcept;
  [[nodiscard]] gneiss_result node_get_local_transform(scene_node_id node,
                                                       engine::transform& out_transform) noexcept;
  [[nodiscard]] gneiss_result node_get_world_transform(scene_node_id node,
                                                       engine::transform& out_transform) noexcept;
  [[nodiscard]] gneiss_result node_get_entity(scene_node_id node, entity_id& out_entity) noexcept;
  [[nodiscard]] gneiss_result node_get_parent(scene_node_id node,
                                              scene_node_id& out_parent) noexcept;
  [[nodiscard]] gneiss_result entity_get_local_transform(entity_id entity,
                                                         engine::transform& out_transform) noexcept;
  [[nodiscard]] gneiss_result
  entity_set_local_transform(entity_id entity, const engine::transform& transform) noexcept;
  [[nodiscard]] gneiss_result
  get_render_snapshot(std::uint32_t viewport_width, std::uint32_t viewport_height,
                      engine::render::render_snapshot& out_snapshot,
                      const engine::render::view_camera* camera) noexcept;

private:
  friend struct engine::world::world_factory;
  explicit world(std::uint32_t domain);
  std::unique_ptr<engine::world::world_state> state_;
};
}
