// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/reflection.h>

#include <stdint.h>
#include <string.h>

static const gneiss_type_id transform_type = {
    {UINT8_C(0x20), UINT8_C(0x00), UINT8_C(0x00), UINT8_C(0x00), UINT8_C(0x00), UINT8_C(0x00),
     UINT8_C(0x00), UINT8_C(0x00), UINT8_C(0x00), UINT8_C(0x00), UINT8_C(0x00), UINT8_C(0x00),
     UINT8_C(0x00), UINT8_C(0x00), UINT8_C(0x00), UINT8_C(0x01)}};
static const gneiss_type_id vector_type = {
    {UINT8_C(0x10), UINT8_C(0x00), UINT8_C(0x00), UINT8_C(0x00), UINT8_C(0x00), UINT8_C(0x00),
     UINT8_C(0x00), UINT8_C(0x00), UINT8_C(0x00), UINT8_C(0x00), UINT8_C(0x00), UINT8_C(0x00),
     UINT8_C(0x00), UINT8_C(0x00), UINT8_C(0x00), UINT8_C(0x01)}};

int main(void) {
  static const char invalid_utf8_name[] = {(char)0xC0, (char)0xAF};
  gneiss_type_registry registry = GNEISS_NULL_TYPE_REGISTRY;
  gneiss_field_desc field = GNEISS_FIELD_DESC_INIT;
  gneiss_field_desc duplicate_fields[2];
  gneiss_type_desc type = GNEISS_TYPE_DESC_INIT;
  gneiss_type_desc invalid_type;
  gneiss_type_desc value_type = GNEISS_TYPE_DESC_INIT;
  gneiss_type_info type_info = {0};
  gneiss_field_info field_info = {0};
  uint32_t type_count = UINT32_C(99);
  uint8_t is_frozen = UINT8_C(1);

  field.id = UINT32_C(1);
  field.value_type_id = vector_type;
  field.name = "position";
  field.name_length = UINT32_C(8);
  type.id = transform_type;
  type.schema_version = UINT32_C(1);
  type.name = "Transform";
  type.name_length = UINT32_C(9);
  type.fields = &field;
  type.field_count = UINT32_C(1);
  value_type.id = vector_type;
  value_type.schema_version = UINT32_C(1);
  value_type.name = "Vec3";
  value_type.name_length = UINT32_C(4);
  duplicate_fields[0] = field;
  duplicate_fields[1] = field;
  invalid_type = type;
  invalid_type.fields = duplicate_fields;
  invalid_type.field_count = UINT32_C(2);

  if (gneiss_type_registry_create(&registry) != GNEISS_SUCCESS ||
      registry == GNEISS_NULL_TYPE_REGISTRY ||
      gneiss_type_registry_is_frozen(registry, &is_frozen) != GNEISS_SUCCESS || is_frozen != 0U ||
      gneiss_type_registry_type_count(registry, &type_count) != GNEISS_ERROR_NOT_READY ||
      type_count != 0U || gneiss_type_registry_register(registry, &type) != GNEISS_SUCCESS ||
      gneiss_type_registry_register(registry, &invalid_type) != GNEISS_ERROR_INVALID_ARGUMENT ||
      gneiss_type_registry_register(registry, &value_type) != GNEISS_SUCCESS ||
      gneiss_type_registry_register(registry, &type) != GNEISS_SUCCESS) {
    return 1;
  }

  type.schema_version = UINT32_C(2);
  if (gneiss_type_registry_register(registry, &type) != GNEISS_ERROR_INVALID_ARGUMENT) {
    return 2;
  }
  type.schema_version = UINT32_C(1);
  invalid_type = value_type;
  invalid_type.name = invalid_utf8_name;
  invalid_type.name_length = UINT32_C(2);
  if (gneiss_type_registry_register(registry, &invalid_type) != GNEISS_ERROR_INVALID_ARGUMENT) {
    return 3;
  }

  if (gneiss_type_registry_freeze(registry) != GNEISS_SUCCESS ||
      gneiss_type_registry_freeze(registry) != GNEISS_SUCCESS ||
      gneiss_type_registry_register(registry, &type) != GNEISS_ERROR_INVALID_STATE ||
      gneiss_type_registry_type_count(registry, &type_count) != GNEISS_SUCCESS ||
      type_count != UINT32_C(2) ||
      gneiss_type_registry_type_at(registry, UINT32_C(0), &type_info) != GNEISS_SUCCESS ||
      memcmp(type_info.id.bytes, vector_type.bytes, sizeof(vector_type.bytes)) != 0 ||
      gneiss_type_registry_find_type(registry, transform_type, &type_info) != GNEISS_SUCCESS ||
      type_info.schema_version != UINT32_C(1) || type_info.field_count != UINT32_C(1) ||
      type_info.name_length != UINT32_C(9) || memcmp(type_info.name, "Transform", 9U) != 0 ||
      gneiss_type_registry_find_field(registry, transform_type, UINT32_C(1), &field_info) !=
          GNEISS_SUCCESS ||
      field_info.name_length != UINT32_C(8) || memcmp(field_info.name, "position", 8U) != 0 ||
      gneiss_type_registry_find_field(registry, transform_type, UINT32_C(2), &field_info) !=
          GNEISS_ERROR_NOT_FOUND) {
    return 4;
  }

  if (gneiss_type_registry_destroy(registry) != GNEISS_SUCCESS ||
      gneiss_type_registry_destroy(registry) != GNEISS_ERROR_INVALID_HANDLE ||
      gneiss_type_registry_type_count(registry, &type_count) != GNEISS_ERROR_INVALID_HANDLE ||
      gneiss_type_registry_create(NULL) != GNEISS_ERROR_INVALID_ARGUMENT) {
    return 5;
  }
  return 0;
}
