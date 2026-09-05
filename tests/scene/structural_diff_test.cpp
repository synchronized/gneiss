// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "scene/structural_diff.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace {

using gneiss::scene_internal::object_description;

object_description object(std::string uuid, std::optional<std::string> parent = std::nullopt) {
  return {.uuid = std::move(uuid),
          .name = "节点",
          .parent_uuid = std::move(parent),
          .translation = {},
          .rotation = {0.0F, 0.0F, 0.0F, 1.0F},
          .scale = {1.0F, 1.0F, 1.0F},
          .camera = std::nullopt,
          .mesh_renderer = std::nullopt};
}

bool test_ordering() {
  using gneiss::scene_internal::build_structural_diff;
  using gneiss::scene_internal::structural_diff;
  const std::vector old_objects{object("old-root"), object("old-child", "old-root")};
  const std::vector new_objects{object("new-child", "new-root"), object("new-root")};
  structural_diff difference;
  return build_structural_diff(old_objects, new_objects, difference) == GNEISS_SUCCESS &&
         difference.added.size() == 2U && difference.added[0].uuid == "new-root" &&
         difference.added[1].uuid == "new-child" && difference.removed.size() == 2U &&
         difference.removed[0].uuid == "old-child" && difference.removed[1].uuid == "old-root";
}

bool test_updates() {
  using namespace gneiss::scene_internal;
  auto previous = object("node", "old-parent");
  previous.name = "旧名称";
  previous.camera = camera_description{.vertical_field_of_view_radians = 1.0F,
                                       .near_plane = 0.1F,
                                       .far_plane = 10.0F,
                                       .is_primary = false};
  auto current = previous;
  current.name = "新名称";
  current.parent_uuid = "new-parent";
  current.translation[0] = 2.0F;
  current.camera.reset();
  current.mesh_renderer =
      mesh_renderer_description{.mesh_uri = "asset://mesh", .material_uri = "asset://material"};
  const std::vector old_objects{object("old-parent"), object("new-parent"), previous};
  const std::vector new_objects{object("new-parent"), object("old-parent"), current};
  structural_diff difference;
  if (build_structural_diff(old_objects, new_objects, difference) != GNEISS_SUCCESS ||
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
  using gneiss::scene_internal::build_structural_diff;
  using gneiss::scene_internal::structural_diff;
  structural_diff difference;
  const std::vector duplicate{object("same"), object("same")};
  const std::vector missing_parent{object("node", "missing")};
  const std::vector cycle{object("first", "second"), object("second", "first")};
  return build_structural_diff({}, duplicate, difference) == GNEISS_ERROR_INVALID_ARGUMENT &&
         difference.empty() &&
         build_structural_diff({}, missing_parent, difference) == GNEISS_ERROR_INVALID_ARGUMENT &&
         difference.empty() &&
         build_structural_diff({}, cycle, difference) == GNEISS_ERROR_INVALID_ARGUMENT &&
         difference.empty();
}

} // namespace

int main() {
  using gneiss::scene_internal::build_structural_diff;
  using gneiss::scene_internal::structural_diff;
  const std::vector unchanged{object("root"), object("child", "root")};
  structural_diff difference;
  if (build_structural_diff(unchanged, unchanged, difference) != GNEISS_SUCCESS ||
      !difference.empty()) {
    return 1;
  }
  return test_ordering() && test_updates() && test_invalid_inputs() ? 0 : 2;
}
