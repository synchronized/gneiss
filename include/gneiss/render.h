// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RENDER_H_
#define GNEISS_RENDER_H_

#include <stdint.h>

#include <gneiss/application.h>
#include <gneiss/core/export.h>
#include <gneiss/core/result.h>
#include <gneiss/core/rid.h>

typedef gneiss_rid gneiss_mesh;
typedef gneiss_rid gneiss_material;

#define GNEISS_NULL_MESH GNEISS_NULL_RID
#define GNEISS_NULL_MATERIAL GNEISS_NULL_RID

/** 首版 Mesh 顶点；位置使用右手坐标，单位由场景约定。 */
typedef struct gneiss_mesh_vertex {
  float x;
  float y;
  float z;
} gneiss_mesh_vertex;

/** Mesh 创建参数。调用期间复制 vertices，调用方保留其所有权。 */
typedef struct gneiss_mesh_desc {
  uint32_t struct_size;
  uint32_t vertex_count;
  const gneiss_mesh_vertex* vertices;
  uint32_t reserved;
} gneiss_mesh_desc;

#define GNEISS_MESH_DESC_INIT {(uint32_t)sizeof(gneiss_mesh_desc), UINT32_C(0), NULL, UINT32_C(0)}

/** 首版固定颜色 Material。颜色分量使用线性空间的 0..1 范围。 */
typedef struct gneiss_material_desc {
  uint32_t struct_size;
  uint32_t reserved;
  float red;
  float green;
  float blue;
  float alpha;
} gneiss_material_desc;

#define GNEISS_MATERIAL_DESC_INIT                                                                  \
  {(uint32_t)sizeof(gneiss_material_desc), UINT32_C(0), 1.0F, 1.0F, 1.0F, 1.0F}

#ifdef __cplusplus
extern "C" {
#endif

/** 在 Application 的 Resource Service 中创建 Mesh；只能在 Application 创建线程调用。 */
GNEISS_API gneiss_result gneiss_mesh_create(gneiss_application application,
                                            const gneiss_mesh_desc* desc, gneiss_mesh* out_mesh);

/** 销毁 Mesh；成功后旧 RID 立即失效。 */
GNEISS_API gneiss_result gneiss_mesh_destroy(gneiss_application application, gneiss_mesh mesh);

/** 在 Application 的 Resource Service 中创建固定颜色 Material。 */
GNEISS_API gneiss_result gneiss_material_create(gneiss_application application,
                                                const gneiss_material_desc* desc,
                                                gneiss_material* out_material);

/** 销毁 Material；成功后旧 RID 立即失效。 */
GNEISS_API gneiss_result gneiss_material_destroy(gneiss_application application,
                                                 gneiss_material material);

#ifdef __cplusplus
}
#endif

#endif
