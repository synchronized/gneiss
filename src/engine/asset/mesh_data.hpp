// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <gneiss/render.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gneiss::asset_internal {

struct mesh_data {
  std::vector<gneiss_mesh_vertex> vertices;
  std::vector<gneiss_mesh_normal> normals;
  std::vector<std::uint32_t> indices;
  std::vector<gneiss_mesh_tangent> tangents{};
  std::vector<gneiss_mesh_uv> uv1{};
  std::vector<gneiss_mesh_color> colors{};
  /** 资产候选与累计驻留使用相同的有效数据字节数，不含容器容量与 GPU 镜像。 */
  [[nodiscard]] std::size_t data_bytes() const noexcept {
    return vertices.size() * sizeof(gneiss_mesh_vertex) +
           normals.size() * sizeof(gneiss_mesh_normal) + indices.size() * sizeof(std::uint32_t) +
           tangents.size() * sizeof(gneiss_mesh_tangent) + uv1.size() * sizeof(gneiss_mesh_uv) +
           colors.size() * sizeof(gneiss_mesh_color);
  }
};

} // namespace gneiss::asset_internal
