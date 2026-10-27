// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/entity_id.hpp"
#include "engine/core/result.hpp"
#include "engine/core/rid.hpp"
#include "engine/core/scene_types.hpp"
#include "engine/function/render/render_snapshot.hpp"
#include "engine/function/world/world_types.hpp"
#include <memory>
#include <span>

namespace gneiss::engine {
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
  [[nodiscard]] gneiss::engine::result entity_create(entity_id& out_entity) noexcept;
  [[nodiscard]] gneiss::engine::result entity_destroy(entity_id entity) noexcept;
  [[nodiscard]] gneiss::engine::result entity_is_alive(entity_id entity,
                                                       bool& out_is_alive) noexcept;
  [[nodiscard]] gneiss::engine::result entity_count(uint64_t& out_count) noexcept;
  [[nodiscard]] gneiss::engine::result
  entity_set_camera(entity_id entity, const gneiss::engine::math::camera_settings& camera,
                    bool primary) noexcept;
  [[nodiscard]] gneiss::engine::result
  entity_configure_camera(entity_id entity,
                          const gneiss::engine::math::camera_settings& desc) noexcept;
  [[nodiscard]] gneiss::engine::result
  entity_get_camera(entity_id entity, gneiss::engine::math::camera_settings& out_camera) noexcept;
  [[nodiscard]] gneiss::engine::result entity_remove_camera(entity_id entity) noexcept;
  [[nodiscard]] gneiss::engine::result set_active_camera(entity_id entity) noexcept;
  [[nodiscard]] gneiss::engine::result get_active_camera(entity_id& out_entity) noexcept;
  [[nodiscard]] gneiss::engine::result replace_mesh_renderers(
      std::span<const gneiss::engine::mesh_renderer_replacement> replacements) noexcept;
  [[nodiscard]] gneiss::engine::result
  entity_set_mesh_renderer(entity_id entity,
                           const gneiss::engine::mesh_renderer& renderer) noexcept;
  [[nodiscard]] gneiss::engine::result entity_remove_mesh_renderer(entity_id entity) noexcept;
  [[nodiscard]] gneiss::engine::result node_create(scene_node_id parent, entity_id entity,
                                                   scene_node_id& out_node) noexcept;
  [[nodiscard]] gneiss::engine::result node_destroy(scene_node_id node) noexcept;
  [[nodiscard]] gneiss::engine::result node_reparent(scene_node_id node,
                                                     scene_node_id parent) noexcept;
  [[nodiscard]] gneiss::engine::result
  node_set_local_transform(scene_node_id node,
                           const gneiss::engine::math::transform& transform) noexcept;
  [[nodiscard]] gneiss::engine::result
  node_get_local_transform(scene_node_id node,
                           gneiss::engine::math::transform& out_transform) noexcept;
  [[nodiscard]] gneiss::engine::result
  node_get_world_transform(scene_node_id node,
                           gneiss::engine::math::transform& out_transform) noexcept;
  [[nodiscard]] gneiss::engine::result node_get_entity(scene_node_id node,
                                                       entity_id& out_entity) noexcept;
  [[nodiscard]] gneiss::engine::result node_get_parent(scene_node_id node,
                                                       scene_node_id& out_parent) noexcept;
  [[nodiscard]] gneiss::engine::result
  entity_get_local_transform(entity_id entity,
                             gneiss::engine::math::transform& out_transform) noexcept;
  [[nodiscard]] gneiss::engine::result
  entity_set_local_transform(entity_id entity,
                             const gneiss::engine::math::transform& transform) noexcept;
  [[nodiscard]] gneiss::engine::result
  get_render_snapshot(std::uint32_t viewport_width, std::uint32_t viewport_height,
                      gneiss::engine::render_snapshot& out_snapshot,
                      const gneiss::engine::view_camera* camera) noexcept;

private:
  friend struct gneiss::engine::world_factory;
  explicit world(std::uint32_t domain);
  std::unique_ptr<gneiss::engine::world_state> state_;
};
}

namespace gneiss::engine {
/** 将实现类型的析构留在 World 模块，不向场景域传播 ECS 后端头。 */
struct GNEISS_API world_deleter {
  void operator()(gneiss::engine::world* state) const noexcept;
};
using world_owner = std::unique_ptr<gneiss::engine::world, world_deleter>;
/** 场景域独占拥有 World；所属线程析构。失败不替换 owner。 */
[[nodiscard]] GNEISS_API result create_native(world_owner& owner) noexcept;

}
