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

/** Texture 像素格式使用定宽整数，避免 C enum 的实现相关 ABI。 */
typedef uint32_t gneiss_texture_format;
#define GNEISS_TEXTURE_FORMAT_RGBA8_UNORM UINT32_C(1)

/** Texture 颜色空间使用定宽整数，避免 C enum 的实现相关 ABI。 */
typedef uint32_t gneiss_texture_color_space;
#define GNEISS_TEXTURE_COLOR_SPACE_LINEAR UINT32_C(1)
#define GNEISS_TEXTURE_COLOR_SPACE_SRGB UINT32_C(2)

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

/** Mesh 顶点；位置使用右手坐标，UV 使用有限二维纹理坐标，允许超出 [0,1] 以支持重复寻址。 */
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

/** 单位切线 XYZ 与副切线手性 W（+1 或 -1），必须与对应法线正交。 */
typedef struct gneiss_mesh_tangent {
  float x;
  float y;
  float z;
  float w;
} gneiss_mesh_tangent;

/** 第二组 UV，允许平铺坐标，分量必须有限。 */
typedef struct gneiss_mesh_uv {
  float u;
  float v;
} gneiss_mesh_uv;
/** 线性 RGBA 顶点色，各分量位于 0..1。 */
typedef struct gneiss_mesh_color {
  float r;
  float g;
  float b;
  float a;
} gneiss_mesh_color;

/** Mesh 创建参数。调用期间复制顶点、可选法线、切线和索引，调用方保留其所有权。 */
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
  uint32_t tangent_count;
  uint32_t reserved_4;
  const gneiss_mesh_tangent* tangents;
  uint32_t uv1_count;
  uint32_t reserved_5;
  const gneiss_mesh_uv* uv1;
  uint32_t color_count;
  uint32_t reserved_6;
  const gneiss_mesh_color* colors;
} gneiss_mesh_desc;

#define GNEISS_MESH_DESC_VERSION_1_SIZE ((uint32_t)offsetof(gneiss_mesh_desc, tangent_count))
#define GNEISS_MESH_DESC_VERSION_2_SIZE ((uint32_t)offsetof(gneiss_mesh_desc, uv1_count))

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
   NULL,                                                                                           \
   UINT32_C(0),                                                                                    \
   UINT32_C(0),                                                                                    \
   NULL,                                                                                           \
   UINT32_C(0),                                                                                    \
   UINT32_C(0),                                                                                    \
   NULL,                                                                                           \
   UINT32_C(0),                                                                                    \
   UINT32_C(0),                                                                                    \
   NULL}

/** 材质 Alpha 模式：不透明、阈值裁剪或透明混合。 */
typedef uint32_t gneiss_material_alpha_mode;
#define GNEISS_MATERIAL_ALPHA_OPAQUE UINT32_C(0)
#define GNEISS_MATERIAL_ALPHA_MASK UINT32_C(1)
#define GNEISS_MATERIAL_ALPHA_BLEND UINT32_C(2)
typedef uint32_t gneiss_texture_filter;
#define GNEISS_TEXTURE_FILTER_NEAREST UINT32_C(0)
#define GNEISS_TEXTURE_FILTER_LINEAR UINT32_C(1)
typedef uint32_t gneiss_texture_mip_filter;
#define GNEISS_TEXTURE_MIP_NONE UINT32_C(0)
#define GNEISS_TEXTURE_MIP_NEAREST UINT32_C(1)
#define GNEISS_TEXTURE_MIP_LINEAR UINT32_C(2)
typedef uint32_t gneiss_texture_address;
#define GNEISS_TEXTURE_ADDRESS_REPEAT UINT32_C(0)
#define GNEISS_TEXTURE_ADDRESS_CLAMP UINT32_C(1)
#define GNEISS_TEXTURE_ADDRESS_MIRROR UINT32_C(2)

/** 逐槽采样状态，不持有资源。UV 仅允许 0/1；枚举不得使用未知值。 */
typedef struct gneiss_texture_sampling {
  uint32_t uv_set;
  gneiss_texture_filter mag_filter;
  gneiss_texture_filter min_filter;
  gneiss_texture_mip_filter mip_filter;
  gneiss_texture_address address_u;
  gneiss_texture_address address_v;
} gneiss_texture_sampling;
#define GNEISS_TEXTURE_SAMPLING_INIT                                                               \
  {UINT32_C(0),                                                                                    \
   GNEISS_TEXTURE_FILTER_LINEAR,                                                                   \
   GNEISS_TEXTURE_FILTER_LINEAR,                                                                   \
   GNEISS_TEXTURE_MIP_LINEAR,                                                                      \
   GNEISS_TEXTURE_ADDRESS_REPEAT,                                                                  \
   GNEISS_TEXTURE_ADDRESS_REPEAT}

/** Material 参数。颜色分量使用线性空间的 0..1 范围，Texture RID 不转移所有权。 */
typedef struct gneiss_material_desc {
  uint32_t struct_size;
  uint32_t reserved;
  float red;
  float green;
  float blue;
  float alpha;
  gneiss_texture base_color_texture;
  float metallic;
  float roughness;
  gneiss_texture metallic_roughness_texture;
  gneiss_texture normal_texture;
  gneiss_texture occlusion_texture;
  gneiss_texture emissive_texture;
  /** 法线 XY 缩放为有限数（允许负值）；AO 强度位于 0..1。 */
  float normal_scale;
  float occlusion_strength;
  /** 线性自发光 RGB，分量位于 0..1；不转移任何 Texture 的所有权。 */
  float emissive[3];
  uint32_t reserved_2;
  gneiss_material_alpha_mode alpha_mode;
  /** 仅允许 0/1；启用时绘制正反面。 */
  uint32_t double_sided;
  /** MASK 阈值，有限非负，默认 0.5。 */
  float alpha_cutoff;
  uint32_t reserved_3;
  /** 顺序：基础颜色、金属粗糙度、法线、AO、自发光。 */
  gneiss_texture_sampling sampling[5];
} gneiss_material_desc;

/** 扩展前布局大小；旧调用方缺少的 PBR 字段使用中性默认值。 */
#define GNEISS_MATERIAL_DESC_VERSION_1_SIZE                                                        \
  ((uint32_t)offsetof(gneiss_material_desc, metallic_roughness_texture))
#define GNEISS_MATERIAL_DESC_VERSION_2_SIZE ((uint32_t)offsetof(gneiss_material_desc, alpha_mode))

#define GNEISS_MATERIAL_DESC_INIT                                                                  \
  {                                                                                                \
    (uint32_t)sizeof(gneiss_material_desc), UINT32_C(0), 1.0F, 1.0F, 1.0F, 1.0F,                   \
        GNEISS_NULL_TEXTURE, 0.0F, 1.0F, GNEISS_NULL_TEXTURE, GNEISS_NULL_TEXTURE,                 \
        GNEISS_NULL_TEXTURE, GNEISS_NULL_TEXTURE, 1.0F, 1.0F, {0.0F, 0.0F, 0.0F}, UINT32_C(0),     \
        GNEISS_MATERIAL_ALPHA_OPAQUE, UINT32_C(0), 0.5F, UINT32_C(0), {                            \
      GNEISS_TEXTURE_SAMPLING_INIT, GNEISS_TEXTURE_SAMPLING_INIT, GNEISS_TEXTURE_SAMPLING_INIT,    \
          GNEISS_TEXTURE_SAMPLING_INIT, GNEISS_TEXTURE_SAMPLING_INIT                               \
    }                                                                                              \
  }

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

/** 即时 UI 顶点；颜色是每字节 RGBA 的打包值。 */
typedef struct gneiss_ui_vertex {
  float position[2];
  float uv[2];
  uint32_t color_rgba8;
} gneiss_ui_vertex;

/** 即时 UI 绘制命令；索引和顶点偏移均相对于本次提交。 */
typedef struct gneiss_ui_draw_command {
  gneiss_texture texture;
  float clip_min[2];
  float clip_max[2];
  uint32_t first_index;
  uint32_t index_count;
  uint32_t vertex_offset;
  uint32_t reserved;
} gneiss_ui_draw_command;

/** 当前帧即时 UI 数据；提交期间深拷贝全部数组，调用方保留所有权。 */
typedef struct gneiss_ui_draw_list_desc {
  uint32_t struct_size;
  uint32_t reserved;
  float display_width;
  float display_height;
  float framebuffer_scale_x;
  float framebuffer_scale_y;
  uint32_t vertex_count;
  const gneiss_ui_vertex* vertices;
  uint32_t index_count;
  const uint32_t* indices;
  uint32_t command_count;
  uint32_t reserved_2;
  const gneiss_ui_draw_command* commands;
} gneiss_ui_draw_list_desc;

#define GNEISS_UI_DRAW_LIST_DESC_VERSION_1_SIZE ((uint32_t)sizeof(gneiss_ui_draw_list_desc))
#define GNEISS_UI_DRAW_LIST_DESC_INIT                                                              \
  {(uint32_t)sizeof(gneiss_ui_draw_list_desc),                                                     \
   UINT32_C(0),                                                                                    \
   0.0F,                                                                                           \
   0.0F,                                                                                           \
   1.0F,                                                                                           \
   1.0F,                                                                                           \
   UINT32_C(0),                                                                                    \
   NULL,                                                                                           \
   UINT32_C(0),                                                                                    \
   NULL,                                                                                           \
   UINT32_C(0),                                                                                    \
   UINT32_C(0),                                                                                    \
   NULL}

/** 世界空间调试线段；颜色为每字节 RGBA，width 为像素宽度。 */
typedef struct gneiss_debug_line {
  float start[3];
  float end[3];
  uint32_t color_rgba8;
  float width;
  uint8_t depth_test;
  uint8_t reserved[3];
} gneiss_debug_line;

/** 当前帧世界调试线段；提交期间深拷贝，调用方保留所有权。 */
typedef struct gneiss_debug_draw_list_desc {
  uint32_t struct_size;
  uint32_t reserved;
  uint32_t line_count;
  uint32_t reserved_2;
  const gneiss_debug_line* lines;
} gneiss_debug_draw_list_desc;

#define GNEISS_DEBUG_DRAW_LIST_DESC_VERSION_1_SIZE ((uint32_t)sizeof(gneiss_debug_draw_list_desc))
#define GNEISS_DEBUG_DRAW_LIST_DESC_INIT                                                           \
  {(uint32_t)sizeof(gneiss_debug_draw_list_desc), UINT32_C(0), UINT32_C(0), UINT32_C(0), NULL}

#ifdef __cplusplus
extern "C" {
#endif

/** 在 Application 的 Resource Service 中创建 Mesh；只能在 Application 创建线程调用。 */
GNEISS_EXPERIMENTAL GNEISS_API gneiss_result gneiss_mesh_create(gneiss_application application,
                                                                const gneiss_mesh_desc* desc,
                                                                gneiss_mesh* out_mesh);

/** 销毁 Mesh；成功后旧 RID 立即失效。 */
GNEISS_EXPERIMENTAL GNEISS_API gneiss_result gneiss_mesh_destroy(gneiss_application application,
                                                                 gneiss_mesh mesh);

/** 在 Application 的 Resource Service 中创建 Material；可选 Texture 必须属于同一 Application。 */
GNEISS_EXPERIMENTAL GNEISS_API gneiss_result
gneiss_material_create(gneiss_application application, const gneiss_material_desc* desc,
                       gneiss_material* out_material);

/** 销毁 Material；成功后旧 RID 立即失效。 */
GNEISS_EXPERIMENTAL GNEISS_API gneiss_result gneiss_material_destroy(gneiss_application application,
                                                                     gneiss_material material);

/** 在 Application 的 Render Service 中创建二维 Texture；像素当前只支持 RGBA8。 */
GNEISS_EXPERIMENTAL GNEISS_API gneiss_result gneiss_texture_create(gneiss_application application,
                                                                   const gneiss_texture_desc* desc,
                                                                   gneiss_texture* out_texture);

/** 销毁 Texture；成功后旧 RID 立即失效。 */
GNEISS_EXPERIMENTAL GNEISS_API gneiss_result gneiss_texture_destroy(gneiss_application application,
                                                                    gneiss_texture texture);

/**
 * 提交当前帧即时 UI 数据。
 *
 * 只能在 Application 创建线程的 update 回调内调用。成功后数据在本帧渲染结束时失效；同一帧再次
 * 成功提交会原子替换前一份数据。所有 Texture RID 必须属于该 Application。
 */
GNEISS_EXPERIMENTAL GNEISS_API gneiss_result gneiss_application_submit_ui_draw_list(
    gneiss_application application, const gneiss_ui_draw_list_desc* desc);

/** 在 update 回调中提交当前帧世界调试线段；同一帧再次提交会原子替换。 */
GNEISS_EXPERIMENTAL GNEISS_API gneiss_result gneiss_application_submit_debug_draw_list(
    gneiss_application application, const gneiss_debug_draw_list_desc* desc);

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
