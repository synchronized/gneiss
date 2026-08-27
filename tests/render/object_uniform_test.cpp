// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "render/granit/object_uniform.h"

#include <cmath>
#include <cstdint>
#include <limits>

namespace {

bool near(float left, float right) noexcept { return std::abs(left - right) < 0.0001F; }

} // namespace

int main() {
  using gneiss::application_internal::build_object_uniform;
  using gneiss::application_internal::calculate_uniform_stride;
  using gneiss::application_internal::object_uniform;
  using gneiss::render_internal::matrix4;

  std::uint64_t stride = 0;
  if (!calculate_uniform_stride(1, stride) || stride != sizeof(object_uniform) ||
      !calculate_uniform_stride(256, stride) || stride != 256 ||
      calculate_uniform_stride(0, stride) ||
      calculate_uniform_stride(std::numeric_limits<std::uint64_t>::max(), stride)) {
    return 1;
  }

  matrix4 identity;
  identity.values = {1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F,
                     0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F};
  gneiss_transform transform = GNEISS_TRANSFORM_IDENTITY;
  transform.translation[0] = 2.0F;
  transform.scale[1] = 2.0F;
  object_uniform uniform;
  if (!build_object_uniform(identity, identity, transform, {1.0F, 0.5F, 0.25F, 1.0F}, uniform) ||
      !near(uniform.model_view_projection.values[12], 2.0F) ||
      !near(uniform.model_view_projection.values[5], 2.0F) ||
      !near(uniform.normal.values[5], 0.5F) || !near(uniform.color[1], 0.5F)) {
    return 2;
  }

  transform.scale[0] = 0.0F;
  if (build_object_uniform(identity, identity, transform, {1.0F, 1.0F, 1.0F, 1.0F}, uniform)) {
    return 3;
  }
  return 0;
}
