// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/render/scene_projection_math.hpp"

#include <cmath>

namespace {

bool near(float left, float right) { return std::abs(left - right) < 0.0001F; }

} // namespace

int main() {
  using gneiss::engine::render::build_model_matrices;
  using gneiss::engine::render::matrix4;

  gneiss::engine::transform transform{};
  transform.translation[0] = 1.0F;
  transform.translation[1] = 2.0F;
  transform.translation[2] = 3.0F;
  transform.rotation[3] = 1.0F;
  transform.scale[0] = 2.0F;
  transform.scale[1] = 3.0F;
  transform.scale[2] = 4.0F;

  matrix4 model;
  matrix4 normal;
  if (!build_model_matrices(transform, model, normal) || !near(model.values[0], 2.0F) ||
      !near(model.values[5], 3.0F) || !near(model.values[10], 4.0F) ||
      !near(model.values[12], 1.0F) || !near(model.values[13], 2.0F) ||
      !near(model.values[14], 3.0F) || !near(normal.values[0], 0.5F) ||
      !near(normal.values[5], 1.0F / 3.0F) || !near(normal.values[10], 0.25F)) {
    return 1;
  }

  // 复用已填充矩阵，负缩放仍须生成正确逆转置，非写入元素不能残留。
  model.values.fill(9.0F);
  normal.values.fill(9.0F);
  transform.scale[0] = -2.0F;
  if (!build_model_matrices(transform, model, normal) || !near(model.values[0], -2.0F) ||
      !near(normal.values[0], -0.5F) || model.values[3] != 0.0F || normal.values[12] != 0.0F ||
      normal.values[15] != 1.0F) {
    return 3;
  }
  const auto previous_model = model;
  const auto previous_normal = normal;
  transform.scale[1] = 0.0F;
  if (build_model_matrices(transform, model, normal) || model.values != previous_model.values ||
      normal.values != previous_normal.values) {
    return 4;
  }
  transform.scale[1] = 3.0F;
  transform.rotation[3] = 0.0F;
  return build_model_matrices(transform, model, normal) ? 2 : 0;
}
