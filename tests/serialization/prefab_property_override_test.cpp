// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "scene/prefab_property_override.h"

#include <gneiss/world.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace {

constexpr auto instance_a = "20000000-0000-4000-8000-000000000001";
constexpr auto instance_b = "20000000-0000-4000-8000-000000000002";
constexpr auto source_node = "10000000-0000-4000-8000-000000000002";

[[nodiscard]] gneiss::scene_internal::prefab_property_override
make_translation(const char* instance, std::array<float, 3> value) {
  return {.key = {.node = {.instance_uuid = instance, .source_node_uuid = source_node},
                  .type_id = gneiss_transform_type_id(),
                  .field_id = GNEISS_TRANSFORM_FIELD_TRANSLATION},
          .value = {.payload = value}};
}

} // namespace

int main() {
  gneiss_type_registry registry = GNEISS_NULL_TYPE_REGISTRY;
  if (gneiss_type_registry_create(&registry) != GNEISS_SUCCESS ||
      gneiss_world_register_reflection(registry) != GNEISS_SUCCESS ||
      gneiss_type_registry_freeze(registry) != GNEISS_SUCCESS) {
    return 1;
  }

  std::vector<gneiss::scene_internal::prefab_property_override> overrides;
  const gneiss::scene_internal::prefab_property_value source{
      .payload = std::array<float, 3>{0.0F, 0.0F, 0.0F}};
  auto second = make_translation(instance_b, {2.0F, 0.0F, 0.0F});
  auto first = make_translation(instance_a, {1.0F, 0.0F, 0.0F});
  if (gneiss::scene_internal::validate_prefab_property_override(registry, first) !=
          GNEISS_SUCCESS ||
      gneiss::scene_internal::set_prefab_property_override(registry, overrides, second, source) !=
          GNEISS_SUCCESS ||
      gneiss::scene_internal::set_prefab_property_override(registry, overrides, first, source) !=
          GNEISS_SUCCESS ||
      overrides.size() != 2U || overrides[0].key.node.instance_uuid != instance_a ||
      overrides[1].key.node.instance_uuid != instance_b) {
    return 2;
  }

  auto updated = make_translation(instance_a, {3.0F, 0.0F, 0.0F});
  if (gneiss::scene_internal::set_prefab_property_override(registry, overrides, updated, source) !=
          GNEISS_SUCCESS ||
      overrides.size() != 2U ||
      std::get<std::array<float, 3>>(overrides[0].value.payload)[0] != 3.0F) {
    return 3;
  }

  auto restored = make_translation(instance_a, {-0.0F, 0.0F, 0.0F});
  if (gneiss::scene_internal::set_prefab_property_override(registry, overrides, restored, source) !=
          GNEISS_SUCCESS ||
      overrides.size() != 1U || overrides[0].key.node.instance_uuid != instance_b) {
    return 4;
  }

  auto wrong_type = make_translation(instance_a, {1.0F, 0.0F, 0.0F});
  wrong_type.value.payload = 1.0F;
  auto invalid_address = make_translation("INVALID", {1.0F, 0.0F, 0.0F});
  auto non_finite =
      make_translation(instance_a, {std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F});
  if (gneiss::scene_internal::validate_prefab_property_override(registry, wrong_type) !=
          GNEISS_ERROR_INVALID_ARGUMENT ||
      gneiss::scene_internal::validate_prefab_property_override(registry, invalid_address) !=
          GNEISS_ERROR_INVALID_ARGUMENT ||
      gneiss::scene_internal::validate_prefab_property_override(registry, non_finite) !=
          GNEISS_ERROR_INVALID_ARGUMENT) {
    return 5;
  }

  gneiss::scene_internal::prefab_property_override read_only{
      .key = {.node = {.instance_uuid = instance_a, .source_node_uuid = source_node},
              .type_id = gneiss_camera_type_id(),
              .field_id = GNEISS_CAMERA_FIELD_IS_PRIMARY},
      .value = {.payload = true}};
  if (gneiss::scene_internal::validate_prefab_property_override(registry, read_only) !=
      GNEISS_ERROR_INVALID_ARGUMENT) {
    return 6;
  }

  auto missing = make_translation(instance_a, {1.0F, 0.0F, 0.0F});
  missing.key.field_id = UINT32_C(999);
  if (gneiss::scene_internal::validate_prefab_property_override(registry, missing) !=
      GNEISS_ERROR_NOT_FOUND) {
    return 7;
  }

  return gneiss_type_registry_destroy(registry) == GNEISS_SUCCESS ? 0 : 8;
}
