// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SCENE_H_
#define GNEISS_SCENE_H_

#include <gneiss/engine/game_module_types.h>
#include <stddef.h>
#include <stdint.h>

#include <gneiss/engine/application.h>
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

/** gneiss_scene_instance_node_info::component_flags 的组件与作者状态位。 */
#define GNEISS_SCENE_NODE_COMPONENT_CAMERA UINT32_C(1)
#define GNEISS_SCENE_NODE_COMPONENT_MESH_RENDERER UINT32_C(2)
#define GNEISS_SCENE_NODE_COMPONENT_PRIMARY_CAMERA UINT32_C(4)
#define GNEISS_SCENE_SUBTREE_MAX_NODES UINT64_C(4096)
#define GNEISS_SCENE_PREFAB_NODE_INSTANCE_ROOT UINT32_C(1)
#define GNEISS_SCENE_PREFAB_NODE_SOURCE_READ_ONLY UINT32_C(2)
#define GNEISS_SCENE_PREFAB_NODE_TRANSLATION_OVERRIDDEN UINT32_C(4)
#define GNEISS_SCENE_PREFAB_NODE_ROTATION_OVERRIDDEN UINT32_C(8)
#define GNEISS_SCENE_PREFAB_NODE_SCALE_OVERRIDDEN UINT32_C(16)

/** 场景实例中的只读节点描述；字符串由实例借出，实例卸载后立即失效。 */
typedef struct gneiss_scene_instance_node_info {
  uint32_t struct_size;
  uint32_t reserved;
  gneiss_scene_node_id node;
  gneiss_scene_node_id parent;
  gneiss_entity_id entity;
  const char* uuid;
  uint64_t uuid_length;
  const char* name;
  uint64_t name_length;
  uint64_t reserved_2[2];
  const char* mesh_uri;
  uint64_t mesh_uri_length;
  const char* material_uri;
  uint64_t material_uri_length;
  gneiss_transform local_transform;
  uint32_t component_flags;
  uint32_t reserved_3;
  gneiss_camera_desc camera;
} gneiss_scene_instance_node_info;

#define GNEISS_SCENE_INSTANCE_NODE_INFO_VERSION_1_SIZE                                             \
  ((uint32_t)offsetof(gneiss_scene_instance_node_info, mesh_uri))
#define GNEISS_SCENE_INSTANCE_NODE_INFO_VERSION_2_SIZE                                             \
  ((uint32_t)offsetof(gneiss_scene_instance_node_info, local_transform))
#define GNEISS_SCENE_INSTANCE_NODE_INFO_VERSION_3_SIZE                                             \
  ((uint32_t)sizeof(gneiss_scene_instance_node_info))
#define GNEISS_SCENE_INSTANCE_NODE_INFO_INIT                                                       \
  {(uint32_t)sizeof(gneiss_scene_instance_node_info),                                              \
   UINT32_C(0),                                                                                    \
   GNEISS_NULL_SCENE_NODE_ID,                                                                      \
   GNEISS_NULL_SCENE_NODE_ID,                                                                      \
   GNEISS_NULL_ENTITY_ID,                                                                          \
   NULL,                                                                                           \
   UINT64_C(0),                                                                                    \
   NULL,                                                                                           \
   UINT64_C(0),                                                                                    \
   {UINT64_C(0), UINT64_C(0)},                                                                     \
   NULL,                                                                                           \
   UINT64_C(0),                                                                                    \
   NULL,                                                                                           \
   UINT64_C(0),                                                                                    \
   GNEISS_TRANSFORM_IDENTITY,                                                                      \
   UINT32_C(0),                                                                                    \
   UINT32_C(0),                                                                                    \
   GNEISS_CAMERA_DESC_INIT}

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

/** 通过 VFS 同步加载、校验并原子实例化场景；失败时 World 保持不变。 */
GNEISS_API gneiss_result gneiss_scene_instance_load(gneiss_application application, const char* uri,
                                                    uint64_t uri_length,
                                                    gneiss_scene_instance* out_instance);

/** 卸载场景创建的实体、节点和资产引用；句柄随后失效。 */
GNEISS_API gneiss_result gneiss_scene_instance_unload(gneiss_application application,
                                                      gneiss_scene_instance instance);

/** 按规范 UUID 查找场景实例中的借用节点 ID。 */
GNEISS_API gneiss_result gneiss_scene_instance_find_node(gneiss_application application,
                                                         gneiss_scene_instance instance,
                                                         const char* uuid, uint64_t uuid_length,
                                                         gneiss_scene_node_id* out_node);

/** 返回实例包含的节点数量；顺序与作者场景 objects 数组一致。 */
GNEISS_API gneiss_result gneiss_scene_instance_get_node_count(gneiss_application application,
                                                              gneiss_scene_instance instance,
                                                              uint64_t* out_count);

/**
 * 按作者顺序读取节点描述。
 *
 * out_info 必须使用 GNEISS_SCENE_INSTANCE_NODE_INFO_INIT
 * 初始化。节点或实体被外部销毁时返回句柄错误。
 * 字符串为场景借用视图；下次场景修改、卸载或父 Application 销毁后须重新获取。
 * 需要跨修改保存时应在修改前复制字符串，不能只复制描述结构中的指针。
 */
GNEISS_API gneiss_result
gneiss_scene_instance_get_node_info(gneiss_application application, gneiss_scene_instance instance,
                                    uint64_t index, gneiss_scene_instance_node_info* out_info);

#ifdef __cplusplus
}
#endif

#endif
