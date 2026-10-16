// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_WORLD_WORLD_SERVICE_HPP_
#define GNEISS_WORLD_WORLD_SERVICE_HPP_
#include "engine/core/publication_group.hpp"
#include "engine/function/render/camera_settings.hpp"
#include <gneiss/engine/scene.h>
#include <gneiss/engine/world.h>
#include <memory>
#include <span>

namespace gneiss::world_internal {
class world_state;
/** 将实现类型的析构留在 World 模块，不向场景域传播 ECS 后端头。 */
struct GNEISS_API world_deleter {
  gneiss_world borrowed{};
  void operator()(world_state* state) const noexcept;
};
using world_owner = std::unique_ptr<world_state, world_deleter>;
/** 场景域拥有真实 World；返回的 RID 仅借用，owner 所属线程析构时自动撤销。失败不替换 owner，输出
 * RID 置零。 */
[[nodiscard]] GNEISS_API gneiss_result create_native(world_owner& owner,
                                                     gneiss_world& borrowed) noexcept;

/** 私有同版本宿主接口，不安装；唯一注册表负责线程和 generation 校验。 */
using camera_settings = render_internal::camera_settings;
struct mesh_renderer_replacement {
  gneiss_entity_id entity{};
  gneiss_mesh_renderer expected{};
  gneiss_mesh_renderer replacement{};
};
/** 不持有资源；调用方保留 renderer 对应租约，候选相机不自动成为活动相机。 */
struct staged_object_description {
  gneiss_scene_node_id parent{};
  gneiss_transform transform = GNEISS_TRANSFORM_IDENTITY;
  const camera_settings* camera{};
  const gneiss_mesh_renderer* renderer{};
};
struct staged_object {
  gneiss_entity_id entity{};
  gneiss_scene_node_id node{};
};
/** 私有所属线程事务入口；失败输出为空；World 必须存活至候选全部清理。 */
[[nodiscard]] GNEISS_API gneiss_result begin_publication(gneiss_world world,
                                                         core::publication& output) noexcept;
[[nodiscard]] GNEISS_API gneiss_result
create_staged_object(gneiss_world world, const core::publication& group,
                     const staged_object_description& description, staged_object& output) noexcept;
/** 只接受本组对象和叶节点，失败不先删除实体。 */
[[nodiscard]] GNEISS_API gneiss_result destroy_staged_object(gneiss_world world,
                                                             const core::publication& group,
                                                             staged_object object) noexcept;
[[nodiscard]] GNEISS_API gneiss_result publish(gneiss_world world,
                                               const core::publication& group) noexcept;
[[nodiscard]] GNEISS_API gneiss_result retire_publication(gneiss_world world,
                                                          const core::publication& group) noexcept;
/** 原子替换等长非空发布组列表；拒绝重复、跨 World、非封闭旧组。
 * 失败保持所有组可见性和主相机；成功后旧组只能逐步销毁，新主相机由调用方选择。 */
[[nodiscard]] GNEISS_API gneiss_result
replace_publications(gneiss_world world, std::span<const core::publication> previous,
                     std::span<const core::publication> candidates) noexcept;
[[nodiscard]] GNEISS_API gneiss_result bind_publication(gneiss_world world,
                                                        const core::publication& group,
                                                        staged_object object) noexcept;
/** 所属线程批量替换已有组件：先验证全部实体及旧 RID，再无分配提交。
 * 不验证渲染服务 RID，调用方必须持有有效资源租约；重复实体拒绝。 */
[[nodiscard]] GNEISS_API gneiss_result replace_mesh_renderers(
    gneiss_world world, std::span<const mesh_renderer_replacement> replacements) noexcept;
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
