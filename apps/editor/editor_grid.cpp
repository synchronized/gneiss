// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "editor_grid.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace gneiss::editor {
namespace {

float fade(float low, float high, float value) noexcept {
  const auto t = std::clamp((value - low) / (high - low), 0.0F, 1.0F);
  return t * t * (3.0F - (2.0F * t));
}

} // namespace

std::vector<debug_line> build_editor_grid(const transform& camera, const gizmo_matrix& view,
                                          float viewport_height) {
  std::vector<debug_line> lines;
  if (!std::isfinite(viewport_height) || viewport_height <= 0.0F ||
      !std::ranges::all_of(camera.translation, [](float value) { return std::isfinite(value); }) ||
      !std::ranges::all_of(view, [](float value) { return std::isfinite(value); })) {
    return lines;
  }
  const auto height = std::abs(camera.translation[1]);
  // 四倍层级保持世界刻度固定；细线在下一层接管前平滑淡出。
  const auto desired = std::clamp(height * 0.1F, 0.25F, 4096.0F);
  const auto level = std::log(desired / 0.25F) / std::log(4.0F);
  const auto spacing = 0.25F * std::pow(4.0F, std::floor(level));
  const auto detail = 1.0F - fade(0.0F, 1.0F, level - std::floor(level));
  const auto radius = desired * 48.0F;
  const auto center_x = std::floor(camera.translation[0] / spacing) * spacing;
  const auto center_z = std::floor(camera.translation[2] / spacing) * spacing;
  const auto count = static_cast<int>(std::ceil(radius / spacing));
  constexpr int segments = 48;
  const auto segment_length = 2.0F * radius / static_cast<float>(segments);
  const auto pixels = viewport_height * 0.8660254F; // 60 度垂直视场角。
  lines.reserve(4096U);
  for (std::size_t axis = 0; axis < 2U; ++axis) {
    for (int index = -count; index <= count; ++index) {
      const std::array center{center_x, center_z};
      const auto fixed = center[axis] + (static_cast<float>(index) * spacing);
      const auto major = std::abs(std::remainder(fixed, spacing * 4.0F)) < spacing * 0.01F;
      const auto next_major = std::abs(std::remainder(fixed, spacing * 16.0F)) < spacing * 0.01F;
      for (int segment = 0; segment < segments; ++segment) {
        const auto begin =
            center[(1 - axis)] - radius + (static_cast<float>(segment) * segment_length);
        const auto middle = begin + (segment_length * 0.5F);
        std::array<float, 2> point{};
        point[axis] = fixed;
        point[1U - axis] = middle;
        const auto x = point[0];
        const auto z = point[1];
        const auto distance = std::hypot(x - camera.translation[0], z - camera.translation[2]);
        const auto depth = -((view[2] * x) + (view[10] * z) + view[14]);
        if (depth <= 0.1F) {
          continue;
        }
        const auto angle = height / std::max(std::hypot(distance, height), 0.001F);
        // 密度、掠视角与范围同时渐隐，避免远端亚像素线条反复出现。
        const auto separation = desired * pixels / depth * angle;
        const auto major_strength = major ? 0.10F + (0.12F * detail) : 0.10F * detail;
        const auto strength = next_major ? 0.22F : major_strength;
        const auto opacity = strength * fade(2.0F, 8.0F, separation) * fade(0.06F, 0.3F, angle) *
                             (1.0F - fade(radius * 0.35F, radius, distance));
        const auto alpha = static_cast<std::uint32_t>(std::lround(opacity * 255.0F));
        if (alpha == 0U) {
          continue;
        }
        debug_line line{};
        const auto fixed_axis = (axis * 2);
        const auto varying_axis = ((1 - axis) * 2);
        line.start[fixed_axis] = fixed;
        line.end[fixed_axis] = fixed;
        line.start[varying_axis] = begin;
        line.end[varying_axis] = begin + segment_length;
        line.color_rgba8 = (alpha << 24U) | 0x00c6b5a6U;
        line.width = 1.0F;
        line.depth_test = 1U;
        lines.push_back(line);
      }
    }
  }
  return lines;
}

} // namespace gneiss::editor
