// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "editor_grid.h"

#include <cmath>
#include <limits>

int main() {
  gneiss::transform camera = GNEISS_TRANSFORM_IDENTITY;
  camera.translation[1] = 3.0F;
  camera.translation[2] = 8.0F;
  gneiss::editor::gizmo_matrix view{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -3, -8, 1};
  const auto lines = gneiss::editor::build_editor_grid(camera, view, 720.0F);
  if (lines.empty() || lines.size() > 40000U) {
    return 1;
  }
  bool distant_major_visible = false;
  bool distant_minor_visible = false;
  for (const auto& line : lines) {
    if (line.start[2] < -9.0F && line.end[2] < -9.0F) {
      distant_major_visible |= line.start[0] == 0.0F && line.end[0] == 0.0F;
      distant_minor_visible |= line.start[0] == 0.25F && line.end[0] == 0.25F;
    }
    if (line.start[1] != 0.0F || line.end[1] != 0.0F || line.depth_test != 1U ||
        (line.color_rgba8 >> 24U) > 57U || !std::isfinite(line.start[0]) ||
        !std::isfinite(line.end[2])) {
      return 2;
    }
  }
  // 中远距离保留主刻度，同时让低于像素密度阈值的细线退隐。
  if (!distant_major_visible || distant_minor_visible) {
    return 8;
  }
  const auto coverage = [](const auto& grid) {
    std::uint64_t total = 0U;
    for (const auto& line : grid) {
      total += line.color_rgba8 >> 24U;
    }
    return total;
  };
  // 同一视图缩小后，应减少细线覆盖，而不是保留同样密集的线条。
  if (coverage(gneiss::editor::build_editor_grid(camera, view, 180.0F)) >= coverage(lines)) {
    return 7;
  }
  if (!gneiss::editor::build_editor_grid(camera, view, 0.0F).empty()) {
    return 3;
  }
  camera.translation[1] = 0.0F;
  if (!gneiss::editor::build_editor_grid(camera, view, 720.0F).empty()) {
    return 4;
  }
  for (const auto height : {0.01F, 2.49F, 2.51F, 9.99F, 10.01F, 1000.0F}) {
    camera.translation[1] = height;
    view[13] = -height;
    if (gneiss::editor::build_editor_grid(camera, view, 720.0F).size() > 40000U) {
      return 5;
    }
  }
  camera.translation[0] = std::numeric_limits<float>::infinity();
  return gneiss::editor::build_editor_grid(camera, view, 720.0F).empty() ? 0 : 6;
}
