// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace gneiss::engine::resource {
struct mesh_vertex {
  float x{}, y{}, z{}, u{}, v{};
};
struct mesh_normal {
  float x{}, y{}, z{};
};
struct mesh_tangent {
  float x{}, y{}, z{}, w{};
};
struct mesh_uv {
  float u{}, v{};
};
struct mesh_color {
  float r{}, g{}, b{}, a{};
};

/** 创建期间借用；校验通过后资源服务复制数组，调用结束后不保留视图。 */
struct mesh_view {
  std::span<const mesh_vertex> vertices;
  std::span<const mesh_normal> normals;
  std::span<const std::uint32_t> indices;
  std::span<const mesh_tangent> tangents;
  std::span<const mesh_uv> uv1;
  std::span<const mesh_color> colors;
};

struct mesh_data {
  std::vector<mesh_vertex> vertices;
  std::vector<mesh_normal> normals;
  std::vector<std::uint32_t> indices;
  std::vector<mesh_tangent> tangents{};
  std::vector<mesh_uv> uv1{};
  std::vector<mesh_color> colors{};
  /** 资产候选与累计驻留使用相同的有效数据字节数，不含容器容量与 GPU 镜像。 */
  [[nodiscard]] std::size_t data_bytes() const noexcept {
    return vertices.size() * sizeof(mesh_vertex) + normals.size() * sizeof(mesh_normal) +
           indices.size() * sizeof(std::uint32_t) + tangents.size() * sizeof(mesh_tangent) +
           uv1.size() * sizeof(mesh_uv) + colors.size() * sizeof(mesh_color);
  }
};

} // namespace gneiss::engine::resource
