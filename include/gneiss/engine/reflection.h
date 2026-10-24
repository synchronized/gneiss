// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_REFLECTION_H_
#define GNEISS_REFLECTION_H_

#include <stddef.h>
#include <stdint.h>

#include <gneiss/engine/core/export.h>
#include <gneiss/engine/core/result.h>

/** 跨构建稳定的 128 位类型标识。全零值表示无效标识。 */
typedef struct gneiss_type_id {
  uint8_t bytes[16];
} gneiss_type_id;

/** 类型内部稳定的字段标识。零值表示无效标识。 */
typedef uint32_t gneiss_field_id;

#define GNEISS_NULL_FIELD_ID UINT32_C(0)

/** 字段当前为只读；后续属性接口不得写入。 */
#define GNEISS_FIELD_FLAG_READ_ONLY UINT32_C(1)

/** 属性值的固定 ABI 类别。 */
typedef uint32_t gneiss_property_kind;
#define GNEISS_PROPERTY_KIND_INVALID UINT32_C(0)
#define GNEISS_PROPERTY_KIND_BOOL UINT32_C(1)
#define GNEISS_PROPERTY_KIND_INT64 UINT32_C(2)
#define GNEISS_PROPERTY_KIND_UINT64 UINT32_C(3)
#define GNEISS_PROPERTY_KIND_FLOAT32 UINT32_C(4)
#define GNEISS_PROPERTY_KIND_FLOAT64 UINT32_C(5)
#define GNEISS_PROPERTY_KIND_STRING UINT32_C(6)
#define GNEISS_PROPERTY_KIND_TYPE_ID UINT32_C(7)
#define GNEISS_PROPERTY_KIND_VEC3 UINT32_C(8)
#define GNEISS_PROPERTY_KIND_QUATERNION UINT32_C(9)

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

#endif
