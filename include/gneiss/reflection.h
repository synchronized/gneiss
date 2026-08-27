// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_REFLECTION_H_
#define GNEISS_REFLECTION_H_

#include <stddef.h>
#include <stdint.h>

#include <gneiss/core/export.h>
#include <gneiss/core/result.h>

/** 跨构建稳定的 128 位类型标识。全零值表示无效标识。 */
typedef struct gneiss_type_id {
  uint8_t bytes[16];
} gneiss_type_id;

/** 类型内部稳定的字段标识。零值表示无效标识。 */
typedef uint32_t gneiss_field_id;

/** Type Registry 的不透明运行时句柄。 */
typedef uint64_t gneiss_type_registry;

#define GNEISS_NULL_FIELD_ID UINT32_C(0)
#define GNEISS_NULL_TYPE_REGISTRY UINT64_C(0)

/** 字段当前为只读；后续属性接口不得写入。 */
#define GNEISS_FIELD_FLAG_READ_ONLY UINT32_C(1)

/** 注册字段时使用的版本化描述。名称必须是无内嵌空字符的 UTF-8；Registry 会深拷贝。 */
typedef struct gneiss_field_desc {
  uint32_t struct_size;
  gneiss_field_id id;
  gneiss_type_id value_type_id;
  uint32_t flags;
  const char* name;
  uint32_t name_length;
} gneiss_field_desc;

#define GNEISS_FIELD_DESC_INIT                                                                     \
  {(uint32_t)sizeof(gneiss_field_desc), GNEISS_NULL_FIELD_ID, {{0}}, UINT32_C(0), NULL, UINT32_C(0)}

/** 注册类型时使用的版本化描述。名称必须是无内嵌空字符的 UTF-8；输入只需在调用期间有效。 */
typedef struct gneiss_type_desc {
  uint32_t struct_size;
  gneiss_type_id id;
  uint32_t schema_version;
  const char* name;
  uint32_t name_length;
  const gneiss_field_desc* fields;
  uint32_t field_count;
} gneiss_type_desc;

#define GNEISS_TYPE_DESC_INIT                                                                      \
  {(uint32_t)sizeof(gneiss_type_desc), {{0}}, UINT32_C(0), NULL, UINT32_C(0), NULL, UINT32_C(0)}

/** 冻结后返回的借用字段元数据；指针在 Registry 销毁前有效。 */
typedef struct gneiss_field_info {
  gneiss_field_id id;
  gneiss_type_id value_type_id;
  uint32_t flags;
  const char* name;
  uint32_t name_length;
} gneiss_field_info;

/** 冻结后返回的借用类型元数据；指针在 Registry 销毁前有效。 */
typedef struct gneiss_type_info {
  gneiss_type_id id;
  uint32_t schema_version;
  const char* name;
  uint32_t name_length;
  const gneiss_field_info* fields;
  uint32_t field_count;
} gneiss_type_info;

#ifdef __cplusplus
extern "C" {
#endif

/** 创建空 Registry。创建和销毁可由任意线程调用，但同一句柄的销毁需要外部同步。 */
GNEISS_API gneiss_result gneiss_type_registry_create(gneiss_type_registry* out_registry);

/** 销毁 Registry；销毁后所有借用元数据立即失效。 */
GNEISS_API gneiss_result gneiss_type_registry_destroy(gneiss_type_registry registry);

/**
 * 深拷贝注册一个类型。只允许在冻结前调用，调用方负责串行化注册。
 *
 * 相同 Type ID 与完整描述的重复注册幂等成功；任何描述冲突返回参数错误。
 */
GNEISS_API gneiss_result gneiss_type_registry_register(gneiss_type_registry registry,
                                                       const gneiss_type_desc* desc);

/** 冻结 Registry。重复冻结幂等成功；冻结成功后不再允许注册。 */
GNEISS_API gneiss_result gneiss_type_registry_freeze(gneiss_type_registry registry);

/** 返回 Registry 是否已经冻结。 */
GNEISS_API gneiss_result gneiss_type_registry_is_frozen(gneiss_type_registry registry,
                                                        uint8_t* out_is_frozen);

/** 返回冻结 Registry 中的类型数量；冻结前返回未就绪。 */
GNEISS_API gneiss_result gneiss_type_registry_type_count(gneiss_type_registry registry,
                                                         uint32_t* out_count);

/** 按 Type ID 字节序返回确定性排序后的类型；返回借用元数据。 */
GNEISS_API gneiss_result gneiss_type_registry_type_at(gneiss_type_registry registry, uint32_t index,
                                                      gneiss_type_info* out_type);

/** 按稳定 Type ID 查询类型；返回借用元数据。 */
GNEISS_API gneiss_result gneiss_type_registry_find_type(gneiss_type_registry registry,
                                                        gneiss_type_id id,
                                                        gneiss_type_info* out_type);

/** 按完整字段身份查询字段；返回借用元数据。 */
GNEISS_API gneiss_result gneiss_type_registry_find_field(gneiss_type_registry registry,
                                                         gneiss_type_id type_id,
                                                         gneiss_field_id field_id,
                                                         gneiss_field_info* out_field);

#ifdef __cplusplus
}
#endif

#endif
