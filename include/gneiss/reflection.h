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

/** 属性值的固定 ABI 类别。 */
typedef enum gneiss_property_kind {
  GNEISS_PROPERTY_KIND_INVALID = 0,
  GNEISS_PROPERTY_KIND_BOOL = 1,
  GNEISS_PROPERTY_KIND_INT64 = 2,
  GNEISS_PROPERTY_KIND_UINT64 = 3,
  GNEISS_PROPERTY_KIND_FLOAT32 = 4,
  GNEISS_PROPERTY_KIND_FLOAT64 = 5,
  GNEISS_PROPERTY_KIND_STRING = 6,
  GNEISS_PROPERTY_KIND_TYPE_ID = 7,
  GNEISS_PROPERTY_KIND_VEC3 = 8,
  GNEISS_PROPERTY_KIND_QUATERNION = 9
} gneiss_property_kind;

#define GNEISS_PROPERTY_CAPABILITY_READABLE UINT32_C(1)
#define GNEISS_PROPERTY_CAPABILITY_WRITABLE UINT32_C(2)

/** 借用 UTF-8 字符串；指针有效期由属性适配器约定。 */
typedef struct gneiss_property_string {
  const char* data;
  uint32_t length;
} gneiss_property_string;

typedef struct gneiss_property_vec3 {
  float x;
  float y;
  float z;
} gneiss_property_vec3;

typedef struct gneiss_property_quaternion {
  float x;
  float y;
  float z;
  float w;
} gneiss_property_quaternion;

typedef union gneiss_property_payload {
  uint8_t bool_value;
  int64_t int64_value;
  uint64_t uint64_value;
  float float32_value;
  double float64_value;
  gneiss_property_string string_value;
  gneiss_type_id type_id_value;
  gneiss_property_vec3 vec3_value;
  gneiss_property_quaternion quaternion_value;
} gneiss_property_payload;

/** 调用方拥有的版本化属性值容器。 */
typedef struct gneiss_property_value {
  uint32_t struct_size;
  uint32_t kind;
  gneiss_property_payload payload;
} gneiss_property_value;

#define GNEISS_PROPERTY_VALUE_INIT                                                                 \
  {(uint32_t)sizeof(gneiss_property_value), GNEISS_PROPERTY_KIND_INVALID, {0}}

/** 属性适配器使用的不透明目标；其含义由适配器定义。 */
typedef struct gneiss_property_target {
  uint64_t context;
  uint64_t object;
} gneiss_property_target;

typedef gneiss_result (*gneiss_property_getter)(void* user_data, gneiss_property_target target,
                                                gneiss_property_value* out_value);
typedef gneiss_result (*gneiss_property_setter)(void* user_data, gneiss_property_target target,
                                                const gneiss_property_value* value);

/** 冻结前绑定到字段的属性访问器；Registry 不拥有 user_data。 */
typedef struct gneiss_property_accessor_desc {
  uint32_t struct_size;
  uint32_t kind;
  gneiss_property_getter getter;
  gneiss_property_setter setter;
  void* user_data;
} gneiss_property_accessor_desc;

#define GNEISS_PROPERTY_ACCESSOR_DESC_INIT                                                         \
  {(uint32_t)sizeof(gneiss_property_accessor_desc), GNEISS_PROPERTY_KIND_INVALID, NULL, NULL, NULL}

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
  uint32_t property_kind;
  uint32_t property_capabilities;
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

/** 冻结前为已注册字段绑定访问器；相同绑定重复调用幂等成功。 */
GNEISS_API gneiss_result gneiss_type_registry_bind_property(
    gneiss_type_registry registry, gneiss_type_id type_id, gneiss_field_id field_id,
    const gneiss_property_accessor_desc* desc);

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

/** 读取冻结 Registry 中的属性；适配器返回的字符串为借用值。 */
GNEISS_API gneiss_result gneiss_type_registry_get_property(gneiss_type_registry registry,
                                                           gneiss_type_id type_id,
                                                           gneiss_field_id field_id,
                                                           gneiss_property_target target,
                                                           gneiss_property_value* out_value);

/** 校验并写入冻结 Registry 中的属性；失败时适配器必须保持目标原值。 */
GNEISS_API gneiss_result gneiss_type_registry_set_property(gneiss_type_registry registry,
                                                           gneiss_type_id type_id,
                                                           gneiss_field_id field_id,
                                                           gneiss_property_target target,
                                                           const gneiss_property_value* value);

#ifdef __cplusplus
}
#endif

#endif
