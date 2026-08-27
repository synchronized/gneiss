// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_WORLD_H_
#define GNEISS_WORLD_H_

#include <stdint.h>

#include <gneiss/core/entity.h>
#include <gneiss/core/export.h>
#include <gneiss/core/result.h>
#include <gneiss/reflection.h>

/** World 的不透明句柄。零值始终表示无效 World。 */
typedef uint64_t gneiss_world;

#define GNEISS_NULL_WORLD UINT64_C(0)

#define GNEISS_TRANSFORM_FIELD_TRANSLATION UINT32_C(1)
#define GNEISS_TRANSFORM_FIELD_ROTATION UINT32_C(2)
#define GNEISS_TRANSFORM_FIELD_SCALE UINT32_C(3)
#define GNEISS_CAMERA_FIELD_VERTICAL_FIELD_OF_VIEW_RADIANS UINT32_C(1)
#define GNEISS_CAMERA_FIELD_NEAR_PLANE UINT32_C(2)
#define GNEISS_CAMERA_FIELD_FAR_PLANE UINT32_C(3)
#define GNEISS_CAMERA_FIELD_IS_PRIMARY UINT32_C(4)

typedef struct gneiss_world_desc {
  uint32_t struct_size;
  uint32_t reserved;
} gneiss_world_desc;

#define GNEISS_WORLD_DESC_INIT {(uint32_t)sizeof(gneiss_world_desc), UINT32_C(0)}

#ifdef __cplusplus
extern "C" {
#endif

/** 创建空 World。World 及其实体只能在创建线程访问。 */
GNEISS_API gneiss_result gneiss_world_create(const gneiss_world_desc* desc,
                                             gneiss_world* out_world);

/** 销毁 World 及其全部实体和组件。 */
GNEISS_API gneiss_result gneiss_world_destroy(gneiss_world world);

/** 在 World 中创建实体。 */
GNEISS_API gneiss_result gneiss_world_entity_create(gneiss_world world,
                                                    gneiss_entity_id* out_entity);

/** 销毁 World 中的实体；旧实体 ID 随即失效。 */
GNEISS_API gneiss_result gneiss_world_entity_destroy(gneiss_world world, gneiss_entity_id entity);

/** 查询实体是否仍属于 World；结果通过 out_is_alive 返回 0 或 1。 */
GNEISS_API gneiss_result gneiss_world_entity_is_alive(gneiss_world world, gneiss_entity_id entity,
                                                      uint8_t* out_is_alive);

/** 返回 World 中当前存活的实体数量。 */
GNEISS_API gneiss_result gneiss_world_entity_count(gneiss_world world, uint64_t* out_count);

/** 返回内建 Transform 的稳定 Type ID。 */
GNEISS_API gneiss_type_id gneiss_transform_type_id(void);

/** 返回内建 Camera 的稳定 Type ID。 */
GNEISS_API gneiss_type_id gneiss_camera_type_id(void);

/**
 * 向冻结前的 Registry 注册 Transform、Camera 及其属性访问器。
 *
 * 属性目标的 context 必须是 World，object 必须是该 World 的实体 ID。重复注册幂等成功。
 */
GNEISS_API gneiss_result gneiss_world_register_reflection(gneiss_type_registry registry);

#ifdef __cplusplus
}
#endif

#endif
