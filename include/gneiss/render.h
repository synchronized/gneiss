// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RENDER_H_
#define GNEISS_RENDER_H_

#include <stddef.h>
#include <stdint.h>

#include <gneiss/application.h>
#include <gneiss/core/export.h>
#include <gneiss/core/result.h>
#include <gneiss/core/rid.h>

typedef gneiss_rid gneiss_mesh;
typedef gneiss_rid gneiss_material;
typedef gneiss_rid gneiss_texture;

#define GNEISS_NULL_MESH GNEISS_NULL_RID
#define GNEISS_NULL_MATERIAL GNEISS_NULL_RID
#define GNEISS_NULL_TEXTURE GNEISS_NULL_RID

typedef enum gneiss_texture_format { GNEISS_TEXTURE_FORMAT_RGBA8_UNORM = 1 } gneiss_texture_format;

typedef enum gneiss_texture_color_space {
  GNEISS_TEXTURE_COLOR_SPACE_LINEAR = 1,
  GNEISS_TEXTURE_COLOR_SPACE_SRGB = 2
} gneiss_texture_color_space;

/** 二维 Texture 创建参数。调用期间复制像素，调用方保留 pixels 所有权。 */
typedef struct gneiss_texture_desc {
  uint32_t struct_size;
  uint32_t format;
  uint32_t color_space;
  uint32_t width;
  uint32_t height;
  uint32_t row_stride_bytes;
  uint64_t pixel_data_size;
  const uint8_t* pixels;
  uint32_t reserved[2];
} gneiss_texture_desc;

#define GNEISS_TEXTURE_DESC_INIT                                                                   \
  {                                                                                                \
    (uint32_t)sizeof(gneiss_texture_desc), GNEISS_TEXTURE_FORMAT_RGBA8_UNORM,                      \
        GNEISS_TEXTURE_COLOR_SPACE_SRGB, UINT32_C(0), UINT32_C(0), UINT32_C(0), UINT64_C(0), NULL, \
    {                                                                                              \
      UINT32_C(0), UINT32_C(0)                                                                     \
    }                                                                                              \
  }

/** Mesh 顶点；位置使用右手坐标，UV 使用归一化二维坐标。 */
typedef struct gneiss_mesh_vertex {
  float x;
  float y;
  float z;
  float u;
  float v;
} gneiss_mesh_vertex;

/** Mesh 单位法线；使用与位置相同的右手坐标。 */
typedef struct gneiss_mesh_normal {
  float x;
  float y;
  float z;
} gneiss_mesh_normal;

/** Mesh v1 创建参数布局，用于兼容已发布的无光照 Mesh 描述。 */
typedef struct gneiss_mesh_desc_version_1 {
  uint32_t struct_size;
  uint32_t vertex_count;
  const gneiss_mesh_vertex* vertices;
  uint32_t reserved;
} gneiss_mesh_desc_version_1;

/** Mesh v2 创建参数布局，用于兼容已发布的可选法线描述。 */
typedef struct gneiss_mesh_desc_version_2 {
  uint32_t struct_size;
  uint32_t vertex_count;
  const gneiss_mesh_vertex* vertices;
  uint32_t reserved;
  uint32_t reserved_2;
  uint32_t normal_count;
  const gneiss_mesh_normal* normals;
} gneiss_mesh_desc_version_2;

/** Mesh 创建参数。调用期间复制顶点、可选法线与可选索引，调用方保留其所有权。 */
typedef struct gneiss_mesh_desc {
  uint32_t struct_size;
  uint32_t vertex_count;
  const gneiss_mesh_vertex* vertices;
  uint32_t reserved;
  uint32_t reserved_2;
  uint32_t normal_count;
  const gneiss_mesh_normal* normals;
  uint32_t index_count;
  uint32_t reserved_3;
  const uint32_t* indices;
} gneiss_mesh_desc;

#define GNEISS_MESH_DESC_VERSION_1_SIZE ((uint32_t)sizeof(gneiss_mesh_desc_version_1))
#define GNEISS_MESH_DESC_VERSION_2_SIZE ((uint32_t)sizeof(gneiss_mesh_desc_version_2))
#define GNEISS_MESH_DESC_INIT                                                                      \
  {(uint32_t)sizeof(gneiss_mesh_desc),                                                             \
   UINT32_C(0),                                                                                    \
   NULL,                                                                                           \
   UINT32_C(0),                                                                                    \
   UINT32_C(0),                                                                                    \
   UINT32_C(0),                                                                                    \
   NULL,                                                                                           \
   UINT32_C(0),                                                                                    \
   UINT32_C(0),                                                                                    \
   NULL}

/** Material 参数。颜色分量使用线性空间的 0..1 范围，Texture RID 不转移所有权。 */
typedef struct gneiss_material_desc {
  uint32_t struct_size;
  uint32_t reserved;
  float red;
  float green;
  float blue;
  float alpha;
  gneiss_texture base_color_texture;
} gneiss_material_desc;

#define GNEISS_MATERIAL_DESC_INIT                                                                  \
  {(uint32_t)sizeof(gneiss_material_desc), UINT32_C(0), 1.0F, 1.0F, 1.0F, 1.0F, GNEISS_NULL_TEXTURE}

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

/** 在 Application 的 Resource Service 中创建 Mesh；只能在 Application 创建线程调用。 */
GNEISS_API gneiss_result gneiss_mesh_create(gneiss_application application,
                                            const gneiss_mesh_desc* desc, gneiss_mesh* out_mesh);

/** 销毁 Mesh；成功后旧 RID 立即失效。 */
GNEISS_API gneiss_result gneiss_mesh_destroy(gneiss_application application, gneiss_mesh mesh);

/** 在 Application 的 Resource Service 中创建 Material；可选 Texture 必须属于同一 Application。 */
GNEISS_API gneiss_result gneiss_material_create(gneiss_application application,
                                                const gneiss_material_desc* desc,
                                                gneiss_material* out_material);

/** 销毁 Material；成功后旧 RID 立即失效。 */
GNEISS_API gneiss_result gneiss_material_destroy(gneiss_application application,
                                                 gneiss_material material);

/** 在 Application 的 Render Service 中创建二维 Texture；像素当前只支持 RGBA8。 */
GNEISS_API gneiss_result gneiss_texture_create(gneiss_application application,
                                               const gneiss_texture_desc* desc,
                                               gneiss_texture* out_texture);

/** 销毁 Texture；成功后旧 RID 立即失效。 */
GNEISS_API gneiss_result gneiss_texture_destroy(gneiss_application application,
                                                gneiss_texture texture);

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

#ifdef __cplusplus
}
#endif

#endif
