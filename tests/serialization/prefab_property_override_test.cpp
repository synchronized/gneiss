// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "engine/function/scene/prefab_property_override.hpp"
#include <gneiss/engine/detail/reflection_values.hpp>

#include "engine/core/reflection/type_registry.hpp"
#include <gneiss/engine/world.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace {

constexpr auto instance_a = "20000000-0000-4000-8000-000000000001";
constexpr auto instance_b = "20000000-0000-4000-8000-000000000002";
constexpr auto source_node = "10000000-0000-4000-8000-000000000002";

[[nodiscard]] gneiss::engine::resource::prefab_property_override
make_translation(const char* instance, std::array<float, 3> value) {
  return {.key = {.node = {.instance_uuid = instance, .source_node_uuid = source_node},
                  .type_id = gneiss::from_native(gneiss_transform_type_id()),
                  .field_id = gneiss::field_id{GNEISS_TRANSFORM_FIELD_TRANSLATION}},
          .value = {.payload = value}};
}

} // namespace

int main() {
  gneiss_type_registry registry = GNEISS_NULL_TYPE_REGISTRY;
  if (gneiss::engine::core::result{gneiss_type_registry_create(&registry)} !=
          gneiss::engine::core::result::success ||
      gneiss::engine::core::result{gneiss_world_register_reflection(registry)} !=
          gneiss::engine::core::result::success ||
      gneiss::engine::core::result{gneiss_type_registry_freeze(registry)} !=
          gneiss::engine::core::result::success) {
    return 1;
  }

  std::vector<gneiss::engine::resource::prefab_property_override> overrides;
  const gneiss::engine::resource::prefab_property_value source{
      .payload = std::array<float, 3>{0.0F, 0.0F, 0.0F}};
  auto second = make_translation(instance_b, {2.0F, 0.0F, 0.0F});
  auto first = make_translation(instance_a, {1.0F, 0.0F, 0.0F});
  if (gneiss::engine::scene::validate_prefab_property_override(
          gneiss::engine::core::reflection::resolve(registry), first) !=
          gneiss::engine::core::result::success ||
      gneiss::engine::scene::set_prefab_property_override(
          gneiss::engine::core::reflection::resolve(registry), overrides, second, source) !=
          gneiss::engine::core::result::success ||
      gneiss::engine::scene::set_prefab_property_override(
          gneiss::engine::core::reflection::resolve(registry), overrides, first, source) !=
          gneiss::engine::core::result::success ||
      overrides.size() != 2U || overrides[0].key.node.instance_uuid != instance_a ||
      overrides[1].key.node.instance_uuid != instance_b) {
    return 2;
  }

  auto updated = make_translation(instance_a, {3.0F, 0.0F, 0.0F});
  if (gneiss::engine::scene::set_prefab_property_override(
          gneiss::engine::core::reflection::resolve(registry), overrides, updated, source) !=
          gneiss::engine::core::result::success ||
      overrides.size() != 2U ||
      std::get<std::array<float, 3>>(overrides[0].value.payload)[0] != 3.0F) {
    return 3;
  }

  auto restored = make_translation(instance_a, {-0.0F, 0.0F, 0.0F});
  if (gneiss::engine::scene::set_prefab_property_override(
          gneiss::engine::core::reflection::resolve(registry), overrides, restored, source) !=
          gneiss::engine::core::result::success ||
      overrides.size() != 1U || overrides[0].key.node.instance_uuid != instance_b) {
    return 4;
  }

  auto wrong_type = make_translation(instance_a, {1.0F, 0.0F, 0.0F});
  wrong_type.value.payload = 1.0F;
  auto invalid_address = make_translation("INVALID", {1.0F, 0.0F, 0.0F});
  auto non_finite =
      make_translation(instance_a, {std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F});
  if (gneiss::engine::scene::validate_prefab_property_override(
          gneiss::engine::core::reflection::resolve(registry), wrong_type) !=
          gneiss::engine::core::result::invalid_argument ||
      gneiss::engine::scene::validate_prefab_property_override(
          gneiss::engine::core::reflection::resolve(registry), invalid_address) !=
          gneiss::engine::core::result::invalid_argument ||
      gneiss::engine::scene::validate_prefab_property_override(
          gneiss::engine::core::reflection::resolve(registry), non_finite) !=
          gneiss::engine::core::result::invalid_argument) {
    return 5;
  }

  gneiss::engine::resource::prefab_property_override read_only{
      .key = {.node = {.instance_uuid = instance_a, .source_node_uuid = source_node},
              .type_id = gneiss::from_native(gneiss_camera_type_id()),
              .field_id = gneiss::field_id{GNEISS_CAMERA_FIELD_IS_PRIMARY}},
      .value = {.payload = true}};
  if (gneiss::engine::scene::validate_prefab_property_override(
          gneiss::engine::core::reflection::resolve(registry), read_only) !=
      gneiss::engine::core::result::invalid_argument) {
    return 6;
  }

  auto missing = make_translation(instance_a, {1.0F, 0.0F, 0.0F});
  missing.key.field_id = gneiss::field_id{999};
  if (gneiss::engine::scene::validate_prefab_property_override(
          gneiss::engine::core::reflection::resolve(registry), missing) !=
      gneiss::engine::core::result::not_found) {
    return 7;
  }

  return gneiss::engine::core::result{gneiss_type_registry_destroy(registry)} ==
                 gneiss::engine::core::result::success
             ? 0
             : 8;
}
