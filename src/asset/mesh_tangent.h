// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <array>
#include <cmath>

namespace gneiss::asset_internal {

[[nodiscard]] inline bool valid_mesh_tangent(const std::array<float, 4>& tangent,
                                             const std::array<float, 3>& normal) noexcept {
  const auto length =
      std::sqrt((tangent[0] * tangent[0]) + (tangent[1] * tangent[1]) + (tangent[2] * tangent[2]));
  const auto dot = (tangent[0] * normal[0]) + (tangent[1] * normal[1]) + (tangent[2] * normal[2]);
  const auto normal_length =
      std::sqrt((normal[0] * normal[0]) + (normal[1] * normal[1]) + (normal[2] * normal[2]));
  return std::isfinite(normal_length) && std::abs(normal_length - 1.0F) <= 1.0e-4F &&
         std::isfinite(length) && std::abs(length - 1.0F) <= 1.0e-4F && std::isfinite(dot) &&
         std::abs(dot) <= 1.0e-4F && (tangent[3] == 1.0F || tangent[3] == -1.0F);
}

} // namespace gneiss::asset_internal
