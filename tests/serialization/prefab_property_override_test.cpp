// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/value_result.hpp"
#include "engine/core/error.hpp"
#include "engine/core/guid.hpp"
#include "engine/function/meta_registration.hpp"
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

const auto source_a = *gneiss::engine::guid::parse("20000000-0000-4000-8000-000000000001");
const auto source_b = *gneiss::engine::guid::parse("20000000-0000-4000-8000-000000000002");

[[nodiscard]] gneiss::engine::prefab_property_override
make_translation(gneiss::engine::guid source_node, gneiss::engine::math::vector3 value) {
  return {.key = {.source_node_uuid = source_node,
                  .type_guid = gneiss::engine::transform_component_type,
                  .property_id = gneiss::engine::transform_translation_field},
          .value = {.payload = value}};
}

} // namespace

int main() {
  if (gneiss::engine::initialize_meta().failed())
    return 1;

  // 共享计算保留其他节点覆盖；恢复原值删除稀疏记录，失败不修改输入。
  {
    using namespace gneiss::engine;
    const mesh_renderer_description original{"asset://base.mesh", "asset://base.material"};
    const mesh_renderer_description replacement{"asset://next.mesh", "asset://next.material"};
    const std::vector existing{make_translation(source_b, {1, 2, 3})};
    auto changed = prepare_mesh_renderer_overrides(source_a, original, replacement, existing);
    if (!changed || changed->size() != 3 || existing.size() != 1)
      return 20;
    auto restored = prepare_mesh_renderer_overrides(source_a, original, original, *changed);
    if (!restored || restored->size() != 1 || restored->front().key.source_node_uuid != source_b)
      return 21;
    auto invalid = prepare_mesh_renderer_overrides({}, original, replacement, existing);
    if (invalid || invalid.error() != error::invalid_argument || existing.size() != 1)
      return 22;
  }

  std::vector<gneiss::engine::prefab_property_override> overrides;
  const gneiss::engine::value source{.payload = gneiss::engine::math::vector3{0.0F, 0.0F, 0.0F}};
  auto second = make_translation(source_b, {2.0F, 0.0F, 0.0F});
  auto first = make_translation(source_a, {1.0F, 0.0F, 0.0F});
  if (gneiss::engine::validate_prefab_property_override(first) != gneiss::engine::error::success ||
      gneiss::engine::set_prefab_property_override(overrides, second, source) !=
          gneiss::engine::error::success ||
      gneiss::engine::set_prefab_property_override(overrides, first, source) !=
          gneiss::engine::error::success ||
      overrides.size() != 2U || overrides[0].key.source_node_uuid != source_a ||
      overrides[1].key.source_node_uuid != source_b) {
    return 2;
  }

  auto updated = make_translation(source_a, {3.0F, 0.0F, 0.0F});
  if (gneiss::engine::set_prefab_property_override(overrides, updated, source) !=
          gneiss::engine::error::success ||
      overrides.size() != 2U ||
      std::get<gneiss::engine::math::vector3>(overrides[0].value.payload)[0] != 3.0F) {
    return 3;
  }

  auto restored = make_translation(source_a, {-0.0F, 0.0F, 0.0F});
  if (gneiss::engine::set_prefab_property_override(overrides, restored, source) !=
          gneiss::engine::error::success ||
      overrides.size() != 1U || overrides[0].key.source_node_uuid != source_b) {
    return 4;
  }

  auto wrong_type = make_translation(source_a, {1.0F, 0.0F, 0.0F});
  wrong_type.value.payload = 1.0F;
  auto invalid_address = make_translation({}, {1.0F, 0.0F, 0.0F});
  auto non_finite =
      make_translation(source_a, {std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F});
  if (gneiss::engine::validate_prefab_property_override(wrong_type) !=
          gneiss::engine::error::invalid_argument ||
      gneiss::engine::validate_prefab_property_override(invalid_address) !=
          gneiss::engine::error::invalid_argument ||
      gneiss::engine::validate_prefab_property_override(non_finite) !=
          gneiss::engine::error::invalid_argument) {
    return 5;
  }

  gneiss::engine::prefab_property_override read_only{
      .key = {.source_node_uuid = source_a,
              .type_guid = gneiss::engine::camera_component_type,
              .property_id = gneiss::engine::meta::property_id{4}},
      .value = {.payload = true}};
  if (gneiss::engine::validate_prefab_property_override(read_only) !=
      gneiss::engine::error::invalid_argument) {
    return 6;
  }

  auto missing = make_translation(source_a, {1.0F, 0.0F, 0.0F});
  missing.key.property_id = gneiss::engine::meta::property_id{999};
  if (gneiss::engine::validate_prefab_property_override(missing) !=
      gneiss::engine::error::not_found) {
    return 7;
  }

  return 0;
}
