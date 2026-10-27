// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "engine/function/scene/prefab_property_override.hpp"
#include "engine/function/world/component_identity.hpp"
#include "engine/function/world/world_reflection.hpp"

#include "engine/core/meta/registration.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace {

constexpr auto instance_a = "20000000-0000-4000-8000-000000000001";
constexpr auto instance_b = "20000000-0000-4000-8000-000000000002";
constexpr auto source_node = "10000000-0000-4000-8000-000000000002";

[[nodiscard]] gneiss::engine::prefab_property_override
make_translation(const char* instance, gneiss::engine::math::vector3 value) {
  return {.key = {.node = {.instance_uuid = instance, .source_node_uuid = source_node},
                  .type_id = gneiss::engine::transform_component_type,
                  .field_id = gneiss::engine::transform_translation_field},
          .value = {.payload = value}};
}

} // namespace

int main() {
  gneiss::engine::meta::registry_ref registry;
  if (gneiss::engine::create_property_registry(registry).failed())
    return 1;

  std::vector<gneiss::engine::prefab_property_override> overrides;
  const gneiss::engine::value source{.payload = gneiss::engine::math::vector3{0.0F, 0.0F, 0.0F}};
  auto second = make_translation(instance_b, {2.0F, 0.0F, 0.0F});
  auto first = make_translation(instance_a, {1.0F, 0.0F, 0.0F});
  if (gneiss::engine::validate_prefab_property_override(registry, first) !=
          gneiss::engine::result::success ||
      gneiss::engine::set_prefab_property_override(registry, overrides, second, source) !=
          gneiss::engine::result::success ||
      gneiss::engine::set_prefab_property_override(registry, overrides, first, source) !=
          gneiss::engine::result::success ||
      overrides.size() != 2U || overrides[0].key.node.instance_uuid != instance_a ||
      overrides[1].key.node.instance_uuid != instance_b) {
    return 2;
  }

  auto updated = make_translation(instance_a, {3.0F, 0.0F, 0.0F});
  if (gneiss::engine::set_prefab_property_override(registry, overrides, updated, source) !=
          gneiss::engine::result::success ||
      overrides.size() != 2U ||
      std::get<gneiss::engine::math::vector3>(overrides[0].value.payload)[0] != 3.0F) {
    return 3;
  }

  auto restored = make_translation(instance_a, {-0.0F, 0.0F, 0.0F});
  if (gneiss::engine::set_prefab_property_override(registry, overrides, restored, source) !=
          gneiss::engine::result::success ||
      overrides.size() != 1U || overrides[0].key.node.instance_uuid != instance_b) {
    return 4;
  }

  auto wrong_type = make_translation(instance_a, {1.0F, 0.0F, 0.0F});
  wrong_type.value.payload = 1.0F;
  auto invalid_address = make_translation("INVALID", {1.0F, 0.0F, 0.0F});
  auto non_finite =
      make_translation(instance_a, {std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F});
  if (gneiss::engine::validate_prefab_property_override(registry, wrong_type) !=
          gneiss::engine::result::invalid_argument ||
      gneiss::engine::validate_prefab_property_override(registry, invalid_address) !=
          gneiss::engine::result::invalid_argument ||
      gneiss::engine::validate_prefab_property_override(registry, non_finite) !=
          gneiss::engine::result::invalid_argument) {
    return 5;
  }

  gneiss::engine::prefab_property_override read_only{
      .key = {.node = {.instance_uuid = instance_a, .source_node_uuid = source_node},
              .type_id = gneiss::engine::camera_component_type,
              .field_id = gneiss::engine::meta::property_id{4}},
      .value = {.payload = true}};
  if (gneiss::engine::validate_prefab_property_override(registry, read_only) !=
      gneiss::engine::result::invalid_argument) {
    return 6;
  }

  auto missing = make_translation(instance_a, {1.0F, 0.0F, 0.0F});
  missing.key.field_id = gneiss::engine::meta::property_id{999};
  if (gneiss::engine::validate_prefab_property_override(registry, missing) !=
      gneiss::engine::result::not_found) {
    return 7;
  }

  return 0;
}
