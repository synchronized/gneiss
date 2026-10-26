// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/math/transform.hpp"

#include <algorithm>
#include <cmath>

namespace {

bool near(float left, float right) { return std::abs(left - right) < 0.0001F; }

} // namespace

int main() {
  using gneiss::engine::matrix4;
  using gneiss::engine::model_and_normal_matrices;

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
  if (!model_and_normal_matrices(transform, model, normal) ||
      !near(gneiss::engine::matrix_elements(model)[0], 2.0F) ||
      !near(gneiss::engine::matrix_elements(model)[5], 3.0F) ||
      !near(gneiss::engine::matrix_elements(model)[10], 4.0F) ||
      !near(gneiss::engine::matrix_elements(model)[12], 1.0F) ||
      !near(gneiss::engine::matrix_elements(model)[13], 2.0F) ||
      !near(gneiss::engine::matrix_elements(model)[14], 3.0F) ||
      !near(gneiss::engine::matrix_elements(normal)[0], 0.5F) ||
      !near(gneiss::engine::matrix_elements(normal)[5], 1.0F / 3.0F) ||
      !near(gneiss::engine::matrix_elements(normal)[10], 0.25F)) {
    return 1;
  }

  // 复用已填充矩阵，负缩放仍须生成正确逆转置，非写入元素不能残留。
  std::ranges::fill(gneiss::engine::matrix_elements(model), 9.0F);
  std::ranges::fill(gneiss::engine::matrix_elements(normal), 9.0F);
  transform.scale[0] = -2.0F;
  if (!model_and_normal_matrices(transform, model, normal) ||
      !near(gneiss::engine::matrix_elements(model)[0], -2.0F) ||
      !near(gneiss::engine::matrix_elements(normal)[0], -0.5F) ||
      gneiss::engine::matrix_elements(model)[3] != 0.0F ||
      gneiss::engine::matrix_elements(normal)[12] != 0.0F ||
      gneiss::engine::matrix_elements(normal)[15] != 1.0F) {
    return 3;
  }
  const auto previous_model = model;
  const auto previous_normal = normal;
  transform.scale[1] = 0.0F;
  if (model_and_normal_matrices(transform, model, normal) || model != previous_model ||
      normal != previous_normal) {
    return 4;
  }
  transform.scale[1] = 3.0F;
  transform.rotation[3] = 0.0F;
  return model_and_normal_matrices(transform, model, normal) ? 2 : 0;
}
