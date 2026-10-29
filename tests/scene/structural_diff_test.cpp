// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/error.hpp"
#include "engine/core/guid.hpp"
#include "engine/function/scene/structural_diff.hpp"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace {

using gneiss::engine::object_description;

gneiss::engine::object_description
object(gneiss::engine::guid uuid, std::optional<gneiss::engine::guid> parent = std::nullopt) {
  return {.uuid = std::move(uuid),
          .name = "节点",
          .parent_uuid = std::move(parent),
          .transform = {},
          .components = {}};
}

bool test_ordering() {
  using gneiss::engine::build_structural_diff;
  using gneiss::engine::structural_diff;
  const std::vector old_objects{
      object(*gneiss::engine::guid::parse("a1c3f975-c0a2-567f-a313-f8a813c837f8")),
      object(*gneiss::engine::guid::parse("d15e74eb-67f6-56eb-9f17-3f906484b4c7"),
             *gneiss::engine::guid::parse("a1c3f975-c0a2-567f-a313-f8a813c837f8"))};
  const std::vector new_objects{
      object(*gneiss::engine::guid::parse("f89fef8a-c435-58da-a9d4-3e93ec3eb4b9"),
             *gneiss::engine::guid::parse("57f469b5-1072-5dc5-aadd-bd86ed451e8c")),
      object(*gneiss::engine::guid::parse("57f469b5-1072-5dc5-aadd-bd86ed451e8c"))};
  structural_diff difference;
  return build_structural_diff(old_objects, new_objects, difference) ==
             gneiss::engine::error::success &&
         difference.added.size() == 2U &&
         difference.added[0].uuid ==
             *gneiss::engine::guid::parse("57f469b5-1072-5dc5-aadd-bd86ed451e8c") &&
         difference.added[1].uuid ==
             *gneiss::engine::guid::parse("f89fef8a-c435-58da-a9d4-3e93ec3eb4b9") &&
         difference.removed.size() == 2U &&
         difference.removed[0].uuid ==
             *gneiss::engine::guid::parse("d15e74eb-67f6-56eb-9f17-3f906484b4c7") &&
         difference.removed[1].uuid ==
             *gneiss::engine::guid::parse("a1c3f975-c0a2-567f-a313-f8a813c837f8");
}

bool test_updates() {
  using namespace gneiss::engine;
  auto previous = object(*gneiss::engine::guid::parse("f75c07c9-9e51-51b7-8dab-cffc2bf231c9"),
                         *gneiss::engine::guid::parse("8b11b657-69db-5c75-9abd-0b8eaedc4e5a"));
  previous.name = "旧名称";
  previous.components.set(gneiss::engine::camera_description{.vertical_field_of_view_radians = 1.0F,
                                                             .near_plane = 0.1F,
                                                             .far_plane = 10.0F,
                                                             .is_primary = false});
  auto current = previous;
  current.name = "新名称";
  current.parent_uuid = *gneiss::engine::guid::parse("16c26687-cd61-552d-bfaf-1b33cc112979");
  current.transform.translation[0] = 2.0F;
  current.components.erase<gneiss::engine::camera_description>();
  current.components.set(gneiss::engine::mesh_renderer_description{
      .mesh_uri = "asset://mesh", .material_uri = "asset://material"});
  const std::vector old_objects{
      object(*gneiss::engine::guid::parse("8b11b657-69db-5c75-9abd-0b8eaedc4e5a")),
      object(*gneiss::engine::guid::parse("16c26687-cd61-552d-bfaf-1b33cc112979")), previous};
  const std::vector new_objects{
      object(*gneiss::engine::guid::parse("16c26687-cd61-552d-bfaf-1b33cc112979")),
      object(*gneiss::engine::guid::parse("8b11b657-69db-5c75-9abd-0b8eaedc4e5a")), current};
  structural_diff difference;
  if (build_structural_diff(old_objects, new_objects, difference) !=
          gneiss::engine::error::success ||
      difference.updated.size() != 1U) {
    return false;
  }
  const auto changes = difference.updated[0].changes;
  return has_change(changes, structural_change::name) &&
         has_change(changes, structural_change::parent) &&
         has_change(changes, structural_change::transform) &&
         has_change(changes, structural_change::camera) &&
         has_change(changes, structural_change::mesh_renderer);
}

bool test_invalid_inputs() {
  using gneiss::engine::build_structural_diff;
  using gneiss::engine::structural_diff;
  structural_diff difference;
  const std::vector duplicate{
      object(*gneiss::engine::guid::parse("b43f0663-3cd9-5687-a1c6-3e5e03782d83")),
      object(*gneiss::engine::guid::parse("b43f0663-3cd9-5687-a1c6-3e5e03782d83"))};
  const std::vector missing_parent{
      object(*gneiss::engine::guid::parse("f75c07c9-9e51-51b7-8dab-cffc2bf231c9"),
             *gneiss::engine::guid::parse("70c7fc22-8299-5ee0-acae-07ded43dadc7"))};
  const std::vector cycle{
      object(*gneiss::engine::guid::parse("8a9fc2b6-7d93-506e-a2ba-140be5cf5982"),
             *gneiss::engine::guid::parse("9b1c5bea-a356-5e02-b2d0-4cd5afb44643")),
      object(*gneiss::engine::guid::parse("9b1c5bea-a356-5e02-b2d0-4cd5afb44643"),
             *gneiss::engine::guid::parse("8a9fc2b6-7d93-506e-a2ba-140be5cf5982"))};
  return build_structural_diff({}, duplicate, difference) ==
             gneiss::engine::error::invalid_argument &&
         difference.empty() &&
         build_structural_diff({}, missing_parent, difference) ==
             gneiss::engine::error::invalid_argument &&
         difference.empty() &&
         build_structural_diff({}, cycle, difference) == gneiss::engine::error::invalid_argument &&
         difference.empty();
}

} // namespace

int main() {
  using gneiss::engine::build_structural_diff;
  using gneiss::engine::structural_diff;
  const std::vector unchanged{
      object(*gneiss::engine::guid::parse("21470280-22a3-5087-9443-62d27dfc4bbf")),
      object(*gneiss::engine::guid::parse("0552e973-b75c-5936-a81b-c56bcd7688d5"),
             *gneiss::engine::guid::parse("21470280-22a3-5087-9443-62d27dfc4bbf"))};
  structural_diff difference;
  if (build_structural_diff(unchanged, unchanged, difference) != gneiss::engine::error::success ||
      !difference.empty()) {
    return 1;
  }
  return test_ordering() && test_updates() && test_invalid_inputs() ? 0 : 2;
}
