// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/asset/mesh_data.hpp"

namespace gneiss::api {

/** 只解析 ABI 布局与借用范围；拓扑和数值校验留在资源服务。失败保留输出。 */
[[nodiscard]] inline gneiss_result
read_mesh_description(const gneiss_mesh_desc& desc, asset_internal::mesh_view& output) noexcept {
  if ((desc.struct_size != GNEISS_MESH_DESC_VERSION_1_SIZE &&
       desc.struct_size != GNEISS_MESH_DESC_VERSION_2_SIZE &&
       desc.struct_size < sizeof(gneiss_mesh_desc)) ||
      desc.reserved != 0U || desc.reserved_2 != 0U || desc.reserved_3 != 0U ||
      desc.vertices == nullptr || ((desc.normal_count == 0U) != (desc.normals == nullptr)) ||
      ((desc.index_count == 0U) != (desc.indices == nullptr))) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  asset_internal::mesh_view value{
      .vertices = {desc.vertices, desc.vertex_count},
      .normals = {desc.normals, desc.normal_count},
      .indices = {desc.indices, desc.index_count},
      .tangents = {},
      .uv1 = {},
      .colors = {},
  };
  if (desc.struct_size >= GNEISS_MESH_DESC_VERSION_2_SIZE) {
    if (desc.reserved_4 != 0U || ((desc.tangent_count == 0U) != (desc.tangents == nullptr))) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    value.tangents = {desc.tangents, desc.tangent_count};
  }
  if (desc.struct_size >= sizeof(gneiss_mesh_desc)) {
    if (desc.reserved_5 != 0U || desc.reserved_6 != 0U ||
        ((desc.uv1_count == 0U) != (desc.uv1 == nullptr)) ||
        ((desc.color_count == 0U) != (desc.colors == nullptr))) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    value.uv1 = {desc.uv1, desc.uv1_count};
    value.colors = {desc.colors, desc.color_count};
  }
  output = value;
  return GNEISS_SUCCESS;
}

} // namespace gneiss::api
