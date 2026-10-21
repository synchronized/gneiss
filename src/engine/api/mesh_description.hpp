// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/resource/types/mesh_data.hpp"
#include <gneiss/engine/render.h>

namespace gneiss::engine::api {

/** 解析 ABI 布局并逐字段复制；拓扑和数值校验留在资源服务。失败保留输出。 */
[[nodiscard]] inline gneiss_result read_mesh_description(const gneiss_mesh_desc& desc,
                                                         engine::resource::mesh_data& output) {
  if ((desc.struct_size != GNEISS_MESH_DESC_VERSION_1_SIZE &&
       desc.struct_size != GNEISS_MESH_DESC_VERSION_2_SIZE &&
       desc.struct_size < sizeof(gneiss_mesh_desc)) ||
      desc.reserved != 0U || desc.reserved_2 != 0U || desc.reserved_3 != 0U ||
      desc.vertices == nullptr || ((desc.normal_count == 0U) != (desc.normals == nullptr)) ||
      ((desc.index_count == 0U) != (desc.indices == nullptr))) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  if (desc.struct_size >= GNEISS_MESH_DESC_VERSION_2_SIZE) {
    if (desc.reserved_4 != 0U || ((desc.tangent_count == 0U) != (desc.tangents == nullptr))) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
  }
  if (desc.struct_size >= sizeof(gneiss_mesh_desc)) {
    if (desc.reserved_5 != 0U || desc.reserved_6 != 0U ||
        ((desc.uv1_count == 0U) != (desc.uv1 == nullptr)) ||
        ((desc.color_count == 0U) != (desc.colors == nullptr))) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
  }
  engine::resource::mesh_data value;
  value.vertices.reserve(desc.vertex_count);
  for (std::uint32_t index = 0; index < desc.vertex_count; ++index) {
    const auto& item = desc.vertices[index];
    value.vertices.push_back({item.x, item.y, item.z, item.u, item.v});
  }
  value.normals.reserve(desc.normal_count);
  for (std::uint32_t index = 0; index < desc.normal_count; ++index) {
    const auto& item = desc.normals[index];
    value.normals.push_back({item.x, item.y, item.z});
  }
  if (desc.index_count != 0U)
    value.indices.assign(desc.indices, desc.indices + desc.index_count);
  if (desc.struct_size >= GNEISS_MESH_DESC_VERSION_2_SIZE) {
    value.tangents.reserve(desc.tangent_count);
    for (std::uint32_t index = 0; index < desc.tangent_count; ++index) {
      const auto& item = desc.tangents[index];
      value.tangents.push_back({item.x, item.y, item.z, item.w});
    }
  }
  if (desc.struct_size >= sizeof(gneiss_mesh_desc)) {
    value.uv1.reserve(desc.uv1_count);
    for (std::uint32_t index = 0; index < desc.uv1_count; ++index) {
      const auto& item = desc.uv1[index];
      value.uv1.push_back({item.u, item.v});
    }
    value.colors.reserve(desc.color_count);
    for (std::uint32_t index = 0; index < desc.color_count; ++index) {
      const auto& item = desc.colors[index];
      value.colors.push_back({item.r, item.g, item.b, item.a});
    }
  }
  output = std::move(value);
  return GNEISS_SUCCESS;
}

} // namespace gneiss::engine::api
