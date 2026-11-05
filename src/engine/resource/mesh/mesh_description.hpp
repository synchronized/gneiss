// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/guid.hpp"
#include "engine/core/math/types.hpp"
#include <string>
#include <string_view>
#include <vector>
namespace gneiss::engine {
/** JSON 网格文件描述；转换为运行期 mesh_data 前校验各数组对应关系。 */
struct mesh_description {
  static constexpr std::string_view type_name = "mesh_description";
  static constexpr guid type_guid = guid::literal("8ce68485-5d9b-477f-9572-db18840e3393");
  std::string topology;
  std::vector<math::vector3> vertices;
  std::vector<math::vector2> uvs;
  std::vector<math::vector3> normals;
};
}
