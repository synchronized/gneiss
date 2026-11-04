// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/math/types.hpp"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace gneiss::engine {
struct mesh_vertex {
  math::vector3 position{};
  math::vector2 uv{};
};

/** 创建期间借用；校验通过后资源服务复制数组，调用结束后不保留视图。 */
struct mesh_view {
  std::span<const mesh_vertex> vertices;
  std::span<const math::vector3> normals;
  std::span<const std::uint32_t> indices;
  std::span<const math::vector4> tangents;
  std::span<const math::vector2> uv1;
  std::span<const math::vector4> colors;
};

struct mesh_data {
  std::vector<mesh_vertex> vertices;
  std::vector<math::vector3> normals;
  std::vector<std::uint32_t> indices;
  std::vector<math::vector4> tangents{};
  std::vector<math::vector2> uv1{};
  std::vector<math::vector4> colors{};
  /** 资产候选与累计驻留使用相同的有效数据字节数，不含容器容量与 GPU 镜像。 */
  [[nodiscard]] std::size_t data_bytes() const noexcept {
    return vertices.size() * sizeof(mesh_vertex) + normals.size() * sizeof(math::vector3) +
           indices.size() * sizeof(std::uint32_t) + tangents.size() * sizeof(math::vector4) +
           uv1.size() * sizeof(math::vector2) + colors.size() * sizeof(math::vector4);
  }
};

[[nodiscard]] inline bool valid_mesh_tangent(const math::vector4& tangent,
                                             const math::vector3& normal) noexcept {
  const auto length =
      std::sqrt((tangent[0] * tangent[0]) + (tangent[1] * tangent[1]) + (tangent[2] * tangent[2]));
  const auto dot = (tangent[0] * normal[0]) + (tangent[1] * normal[1]) + (tangent[2] * normal[2]);
  const auto normal_length =
      std::sqrt((normal[0] * normal[0]) + (normal[1] * normal[1]) + (normal[2] * normal[2]));
  return std::isfinite(normal_length) && std::abs(normal_length - 1.0F) <= 1.0e-4F &&
         std::isfinite(length) && std::abs(length - 1.0F) <= 1.0e-4F && std::isfinite(dot) &&
         std::abs(dot) <= 1.0e-4F && (tangent[3] == 1.0F || tangent[3] == -1.0F);
}

} // namespace gneiss::engine
