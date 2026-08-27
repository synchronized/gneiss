// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "render/normal_math.h"

#include <cmath>

namespace gneiss::render_internal {

std::array<float, 3> transform_normal(const gneiss_mesh_normal& normal,
                                      const gneiss_transform& transform) noexcept {
  std::array value{normal.x / transform.scale[0], normal.y / transform.scale[1],
                   normal.z / transform.scale[2]};
  const std::array vector{transform.rotation[0], transform.rotation[1], transform.rotation[2]};
  const std::array first_cross{(vector[1] * value[2]) - (vector[2] * value[1]),
                               (vector[2] * value[0]) - (vector[0] * value[2]),
                               (vector[0] * value[1]) - (vector[1] * value[0])};
  const std::array second_cross{(vector[1] * first_cross[2]) - (vector[2] * first_cross[1]),
                                (vector[2] * first_cross[0]) - (vector[0] * first_cross[2]),
                                (vector[0] * first_cross[1]) - (vector[1] * first_cross[0])};
  for (std::size_t index = 0; index < value.size(); ++index) {
    value[index] += 2.0F * ((transform.rotation[3] * first_cross[index]) + second_cross[index]);
  }
  const auto inverse_length =
      1.0F / std::sqrt((value[0] * value[0]) + (value[1] * value[1]) + (value[2] * value[2]));
  for (auto& component : value) {
    component *= inverse_length;
  }
  return value;
}

} // namespace gneiss::render_internal
