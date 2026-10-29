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
  [[nodiscard]] error entity_create(entity_id& out_entity) noexcept;
  [[nodiscard]] error entity_destroy(entity_id entity) noexcept;
  [[nodiscard]] result<bool> entity_is_alive(entity_id entity) noexcept;
  [[nodiscard]] result<std::uint64_t> entity_count() noexcept;
  [[nodiscard]] error entity_set_camera(entity_id entity, const math::camera_settings& camera,
                                        bool primary) noexcept;
  [[nodiscard]] error entity_configure_camera(entity_id entity,
                                              const math::camera_settings& desc) noexcept;
  [[nodiscard]] error entity_get_camera(entity_id entity,
                                        math::camera_settings& out_camera) noexcept;
  [[nodiscard]] error entity_remove_camera(entity_id entity) noexcept;
  [[nodiscard]] error set_active_camera(entity_id entity) noexcept;
  [[nodiscard]] error get_active_camera(entity_id& out_entity) noexcept;
  [[nodiscard]] error
  replace_mesh_renderers(std::span<const mesh_renderer_replacement> replacements) noexcept;
  [[nodiscard]] error entity_set_mesh_renderer(entity_id entity,
                                               const mesh_renderer& renderer) noexcept;
  [[nodiscard]] error entity_remove_mesh_renderer(entity_id entity) noexcept;
  /** 创建节点与关联实体；失败不留下部分对象。限所属线程。 */
  [[nodiscard]] result<scene_node_id> object_create(scene_node_id parent,
                                                    scene_node_identity identity,
                                                    const math::transform& transform) noexcept;
  /** 销毁整棵子树及关联实体，释放 ECS 组件引用。 */
  [[nodiscard]] error object_destroy(scene_node_id node) noexcept;
  [[nodiscard]] error node_set_identity(scene_node_id node, scene_node_identity identity) noexcept;
  /** 借用名称与身份，仅在节点存活且元数据未修改期间有效。 */
  [[nodiscard]] result<scene_node_identity_view> node_identity(scene_node_id node) const noexcept;
  [[nodiscard]] result<mesh_renderer> node_mesh_renderer(scene_node_id node) noexcept;
  [[nodiscard]] error node_create(scene_node_id parent, entity_id entity,
                                  scene_node_id& out_node) noexcept;
  [[nodiscard]] error node_destroy(scene_node_id node) noexcept;
  [[nodiscard]] error node_reparent(scene_node_id node, scene_node_id parent) noexcept;
  [[nodiscard]] error node_set_local_transform(scene_node_id node,
                                               const math::transform& transform) noexcept;
  [[nodiscard]] error node_get_local_transform(scene_node_id node,
                                               math::transform& out_transform) noexcept;
  [[nodiscard]] error node_get_world_transform(scene_node_id node,
                                               math::transform& out_transform) noexcept;
  [[nodiscard]] error node_get_entity(scene_node_id node, entity_id& out_entity) noexcept;
  [[nodiscard]] error node_get_parent(scene_node_id node, scene_node_id& out_parent) noexcept;
  [[nodiscard]] error entity_get_local_transform(entity_id entity,
                                                 math::transform& out_transform) noexcept;
  [[nodiscard]] error entity_set_local_transform(entity_id entity,
                                                 const math::transform& transform) noexcept;
  [[nodiscard]] error get_render_snapshot(std::uint32_t viewport_width,
                                          std::uint32_t viewport_height,
                                          render_snapshot& out_snapshot,
                                          const view_camera* camera) noexcept;

private:
  friend struct engine::world_factory;
  explicit world(std::uint32_t domain);
  std::unique_ptr<engine::world_state> state_;
};
}

namespace gneiss::engine {
/** 将实现类型的析构留在 World 模块，不向场景域传播 ECS 后端头。 */
struct GNEISS_API world_deleter {
  void operator()(engine::world* state) const noexcept;
};
using world_owner = std::unique_ptr<engine::world, world_deleter>;
/** 创建独立 World 的拥有者；必须在所属线程析构。 */
[[nodiscard]] GNEISS_API result<world_owner> create_native() noexcept;
}
