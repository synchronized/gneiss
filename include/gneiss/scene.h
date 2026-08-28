// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SCENE_H_
#define GNEISS_SCENE_H_

#include <stdint.h>

#include <gneiss/application.h>
#include <gneiss/core/entity.h>
#include <gneiss/core/export.h>
#include <gneiss/core/result.h>
#include <gneiss/world.h>

/** Scene Node 的运行时标识；零值表示无效节点。 */
typedef uint64_t gneiss_scene_node_id;
/** 已加载场景实例的不透明句柄；由所属 Application 独占。 */
typedef uint64_t gneiss_scene_instance;

#define GNEISS_NULL_SCENE_NODE_ID UINT64_C(0)
#define GNEISS_NULL_SCENE_INSTANCE UINT64_C(0)

/**
 * Scene Tree 中的局部或世界变换。旋转使用归一化的 (x, y, z, w) 四元数，缩放各轴不得为零。
 */
typedef struct gneiss_transform {
  float translation[3];
  float rotation[4];
  float scale[3];
} gneiss_transform;

#define GNEISS_TRANSFORM_IDENTITY {{0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F, 1.0F}, {1.0F, 1.0F, 1.0F}}

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

/**
 * 将实例当前 Transform 与 Camera 写入当前版本的 UTF-8 场景 JSON。
 *
 * buffer 为空且 capacity 为零时只查询所需字节数；长度不包含字符串终止符。未知作者字段会被保留。
 */
GNEISS_API gneiss_result gneiss_scene_instance_serialize(gneiss_application application,
                                                         gneiss_scene_instance instance,
                                                         char* buffer, uint64_t capacity,
                                                         uint64_t* out_length);

#ifdef __cplusplus
}
#endif

#endif
