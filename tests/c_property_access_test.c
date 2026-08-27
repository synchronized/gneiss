// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/reflection.h>

#include <stdint.h>

static gneiss_result get_value(void* user_data, gneiss_property_target target,
                               gneiss_property_value* out_value) {
  if (target.context != UINT64_C(3) || target.object != UINT64_C(5)) {
    return GNEISS_ERROR_NOT_FOUND;
  }
  out_value->kind = GNEISS_PROPERTY_KIND_INT64;
  out_value->payload.int64_value = *(const int64_t*)user_data;
  return GNEISS_SUCCESS;
}

static gneiss_result set_value(void* user_data, gneiss_property_target target,
                               const gneiss_property_value* value) {
  if (target.context != UINT64_C(3) || target.object != UINT64_C(5)) {
    return GNEISS_ERROR_NOT_FOUND;
  }
  if (value->payload.int64_value < INT64_C(-10) || value->payload.int64_value > INT64_C(10)) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *(int64_t*)user_data = value->payload.int64_value;
  return GNEISS_SUCCESS;
}

int main(void) {
  const gneiss_type_id type_id = {
      {1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U, 11U, 12U, 13U, 14U, 15U, 16U}};
  const gneiss_type_id value_type_id = {{1U}};
  const gneiss_field_desc field = {
      sizeof(gneiss_field_desc), UINT32_C(1), value_type_id, UINT32_C(0), "value", UINT32_C(5)};
  const gneiss_type_desc type = {
      sizeof(gneiss_type_desc), type_id, UINT32_C(1), "test", UINT32_C(4), &field, UINT32_C(1)};
  int64_t storage = INT64_C(4);
  const gneiss_property_accessor_desc accessor = {sizeof(gneiss_property_accessor_desc),
                                                  GNEISS_PROPERTY_KIND_INT64, get_value, set_value,
                                                  &storage};
  const gneiss_property_target target = {UINT64_C(3), UINT64_C(5)};
  gneiss_property_value value = GNEISS_PROPERTY_VALUE_INIT;
  gneiss_type_registry registry = GNEISS_NULL_TYPE_REGISTRY;

  if (gneiss_type_registry_create(&registry) != GNEISS_SUCCESS ||
      gneiss_type_registry_register(registry, &type) != GNEISS_SUCCESS ||
      gneiss_type_registry_bind_property(registry, type_id, UINT32_C(1), &accessor) !=
          GNEISS_SUCCESS ||
      gneiss_type_registry_freeze(registry) != GNEISS_SUCCESS ||
      gneiss_type_registry_get_property(registry, type_id, UINT32_C(1), target, &value) !=
          GNEISS_SUCCESS ||
      value.kind != GNEISS_PROPERTY_KIND_INT64 || value.payload.int64_value != INT64_C(4)) {
    return 1;
  }
  value.kind = GNEISS_PROPERTY_KIND_INT64;
  value.payload.int64_value = INT64_C(8);
  if (gneiss_type_registry_set_property(registry, type_id, UINT32_C(1), target, &value) !=
          GNEISS_SUCCESS ||
      storage != INT64_C(8)) {
    return 2;
  }
  value.payload.int64_value = INT64_C(20);
  if (gneiss_type_registry_set_property(registry, type_id, UINT32_C(1), target, &value) !=
          GNEISS_ERROR_INVALID_ARGUMENT ||
      storage != INT64_C(8)) {
    return 3;
  }
  return gneiss_type_registry_destroy(registry) == GNEISS_SUCCESS ? 0 : 4;
}
