// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SCENE_H_
#define GNEISS_SCENE_H_

#include <gneiss/engine/game_module_types.h>
#include <stddef.h>
#include <stdint.h>

#include <gneiss/engine/core/entity.h>
#include <gneiss/engine/core/export.h>
#include <gneiss/engine/core/result.h>
#include <gneiss/engine/render.h>
#include <gneiss/engine/world.h>

/** Scene Node 的运行时标识；零值表示无效节点。 */
typedef uint64_t gneiss_scene_node_id;
/** 已加载场景实例的不透明句柄；由所属 Application 独占。 */
typedef uint64_t gneiss_scene_instance;
/** Prefab 刷新事务令牌；由所属场景实例独占。 */
typedef uint64_t gneiss_scene_prefab_refresh_token;

#define GNEISS_NULL_SCENE_NODE_ID UINT64_C(0)
#define GNEISS_NULL_SCENE_INSTANCE UINT64_C(0)
#define GNEISS_NULL_SCENE_PREFAB_REFRESH_TOKEN UINT64_C(0)

/** 迁移期组件与作者状态位；原生类型见 src/engine/core/scene_types.hpp。 */
#define GNEISS_SCENE_NODE_COMPONENT_CAMERA UINT32_C(1)
#define GNEISS_SCENE_NODE_COMPONENT_MESH_RENDERER UINT32_C(2)
#define GNEISS_SCENE_NODE_COMPONENT_PRIMARY_CAMERA UINT32_C(4)
#define GNEISS_SCENE_SUBTREE_MAX_NODES UINT64_C(4096)
#define GNEISS_SCENE_PREFAB_NODE_INSTANCE_ROOT UINT32_C(1)
#define GNEISS_SCENE_PREFAB_NODE_SOURCE_READ_ONLY UINT32_C(2)
#define GNEISS_SCENE_PREFAB_NODE_TRANSLATION_OVERRIDDEN UINT32_C(4)
#define GNEISS_SCENE_PREFAB_NODE_ROTATION_OVERRIDDEN UINT32_C(8)
#define GNEISS_SCENE_PREFAB_NODE_SCALE_OVERRIDDEN UINT32_C(16)

#ifdef __cplusplus
extern "C" {
#endif

/** 创建节点。parent 为零时创建根节点；entity 为零时不关联实体。 */
GNEISS_API gneiss_result gneiss_scene_node_create(gneiss_world world, gneiss_scene_node_id parent,
                                                  gneiss_entity_id entity,
                                                  gneiss_scene_node_id* out_node);

/** 递归销毁节点及其子节点；关联实体仍由 World 持有。 */
GNEISS_API gneiss_result gneiss_scene_node_destroy(gneiss_world world, gneiss_scene_node_id node);

/** 更换父节点。parent 为零时变为根节点；拒绝形成循环。 */
GNEISS_API gneiss_result gneiss_scene_node_reparent(gneiss_world world, gneiss_scene_node_id node,
                                                    gneiss_scene_node_id parent);

/** 设置节点局部变换；拒绝非有限值、非归一化旋转和任一轴为零的缩放。 */
GNEISS_API gneiss_result gneiss_scene_node_set_local_transform(gneiss_world world,
                                                               gneiss_scene_node_id node,
                                                               const gneiss_transform* transform);

/** 获取节点局部变换。 */
GNEISS_API gneiss_result gneiss_scene_node_get_local_transform(gneiss_world world,
                                                               gneiss_scene_node_id node,
                                                               gneiss_transform* out_transform);

/** 获取沿父链组合后的世界变换。 */
GNEISS_API gneiss_result gneiss_scene_node_get_world_transform(gneiss_world world,
                                                               gneiss_scene_node_id node,
                                                               gneiss_transform* out_transform);

/** 获取节点当前关联的实体；实体被销毁后返回零值。 */
GNEISS_API gneiss_result gneiss_scene_node_get_entity(gneiss_world world, gneiss_scene_node_id node,
                                                      gneiss_entity_id* out_entity);

/** 获取节点当前父节点；根节点成功返回零值。 */
GNEISS_API gneiss_result gneiss_scene_node_get_parent(gneiss_world world, gneiss_scene_node_id node,
                                                      gneiss_scene_node_id* out_parent);

/** 通过实体关联读取 Scene Tree 节点的局部 Transform。 */
GNEISS_API gneiss_result gneiss_world_entity_get_local_transform(gneiss_world world,
                                                                 gneiss_entity_id entity,
                                                                 gneiss_transform* out_transform);

/** 通过实体关联写入 Scene Tree 节点的局部 Transform。 */
GNEISS_API gneiss_result gneiss_world_entity_set_local_transform(gneiss_world world,
                                                                 gneiss_entity_id entity,
                                                                 const gneiss_transform* transform);

#ifdef __cplusplus
}
#endif

#endif
