// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "mesh_description.hpp"
#include <cmath>
#include <limits>
namespace gneiss {
namespace {
serialization::diagnostic invalid_description(std::string path, std::string message) {
  return {.path = std::move(path), .message = std::move(message), .error = error::invalid_argument};
}
}
serialization::diagnostic validate_mesh_description(const mesh_description& v) {
  if (v.topology != "triangle_list")
    return invalid_description("/topology", "只支持 triangle_list");
  const auto count = v.vertices.size();
  if (count < 3 || count % 3 != 0 || count > std::numeric_limits<std::uint32_t>::max())
    return invalid_description("/vertices", "顶点必须组成非空三角形列表");
  if (v.uvs.size() != count)
    return invalid_description("/uvs", "UV 数量必须与顶点数量一致");
  if (v.normals.size() != count)
    return invalid_description("/normals", "法线数量必须与顶点数量一致");
  for (std::size_t i = 0; i < count; ++i) {
    const auto& n = v.normals[i];
    const auto length = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
    if (!std::isfinite(length) || std::abs(length - 1.0F) > 1.0e-4F)
      return invalid_description("/normals/" + std::to_string(i), "法线必须归一化");
  }
  return {};
}
}
