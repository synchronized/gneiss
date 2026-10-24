// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RENDER_H_
#define GNEISS_RENDER_H_

#include <stddef.h>
#include <stdint.h>

#include <gneiss/engine/core/export.h>
#include <gneiss/engine/core/result.h>
#include <gneiss/engine/core/rid.h>
#include <gneiss/engine/world.h>

typedef gneiss_rid gneiss_mesh;
typedef gneiss_rid gneiss_material;
typedef gneiss_rid gneiss_texture;

#define GNEISS_NULL_MESH GNEISS_NULL_RID
#define GNEISS_NULL_MATERIAL GNEISS_NULL_RID
#define GNEISS_NULL_TEXTURE GNEISS_NULL_RID

/** 透视 Camera 参数。首版只允许一个 primary Camera 参与渲染。 */
typedef struct gneiss_camera {
  float vertical_field_of_view_radians;
  float near_plane;
  float far_plane;
  uint8_t is_primary;
  uint8_t reserved[3];
} gneiss_camera;

#define GNEISS_CAMERA_INIT {1.04719755F, 0.1F, 1000.0F, UINT8_C(1), {0, 0, 0}}

/** 版本化透视 Camera 描述；活动 Camera 由 World 独立选择。 */
typedef struct gneiss_camera_desc {
  uint32_t struct_size;
  uint32_t reserved;
  float vertical_field_of_view_radians;
  float near_plane;
  float far_plane;
} gneiss_camera_desc;

#define GNEISS_CAMERA_DESC_INIT                                                                    \
  {(uint32_t)sizeof(gneiss_camera_desc), UINT32_C(0), 1.04719755F, 0.1F, 1000.0F}

/** 实体引用的 Mesh 与 Material；二者只保存 RID，不拥有资源。 */
typedef struct gneiss_mesh_renderer {
  gneiss_mesh mesh;
  gneiss_material material;
} gneiss_mesh_renderer;

#define GNEISS_MESH_RENDERER_INIT {GNEISS_NULL_MESH, GNEISS_NULL_MATERIAL}

#ifdef __cplusplus
extern "C" {
#endif

/** 设置或替换实体的 Camera 组件。World 和实体必须属于当前线程。 */
GNEISS_API gneiss_result gneiss_world_entity_set_camera(gneiss_world world, gneiss_entity_id entity,
                                                        const gneiss_camera* camera);

/** 使用版本化描述设置或替换 Camera；不改变 World 当前的活动 Camera。 */
GNEISS_API gneiss_result gneiss_world_entity_configure_camera(gneiss_world world,
                                                              gneiss_entity_id entity,
                                                              const gneiss_camera_desc* desc);

/** 获取 Camera 描述；out_camera 必须以 GNEISS_CAMERA_DESC_INIT 初始化。 */
GNEISS_API gneiss_result gneiss_world_entity_get_camera(gneiss_world world, gneiss_entity_id entity,
                                                        gneiss_camera_desc* out_camera);

/** 移除实体的 Camera；若其为活动 Camera，World 随即变为无活动 Camera。 */
GNEISS_API gneiss_result gneiss_world_entity_remove_camera(gneiss_world world,
                                                           gneiss_entity_id entity);

/** 选择 World 的活动 Camera；实体必须属于 World 且已经配置 Camera。 */
GNEISS_API gneiss_result gneiss_world_set_active_camera(gneiss_world world,
                                                        gneiss_entity_id entity);

/** 获取借用的活动 Camera 实体 ID；未选择时返回零值和 GNEISS_ERROR_NOT_READY。 */
GNEISS_API gneiss_result gneiss_world_get_active_camera(gneiss_world world,
                                                        gneiss_entity_id* out_entity);

/** 设置或替换实体的 Mesh Renderer 组件；RID 在渲染提取阶段校验。 */
GNEISS_API gneiss_result gneiss_world_entity_set_mesh_renderer(
    gneiss_world world, gneiss_entity_id entity, const gneiss_mesh_renderer* renderer);

/** 移除实体的 Mesh Renderer；组件不存在时返回 GNEISS_ERROR_NOT_FOUND。 */
GNEISS_API gneiss_result gneiss_world_entity_remove_mesh_renderer(gneiss_world world,
                                                                  gneiss_entity_id entity);

#ifdef __cplusplus
}
#endif

#endif
