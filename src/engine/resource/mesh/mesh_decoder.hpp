// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/resource/asset_diagnostic.hpp"
#include "engine/resource/mesh/mesh_data.hpp"
namespace gneiss::engine::detail {
[[nodiscard]] result parse_mesh(const std::vector<std::byte>& bytes,
                                std::vector<mesh_vertex>& out_vertices,
                                std::vector<math::vector3>& out_normals,
                                asset_diagnostic& diagnostic);

[[nodiscard]] result
parse_binary_mesh(const std::vector<std::byte>& bytes, std::vector<mesh_vertex>& out_vertices,
                  std::vector<math::vector3>& out_normals, std::vector<std::uint32_t>& out_indices,
                  std::vector<math::vector4>& out_tangents, std::vector<math::vector2>& out_uv1,
                  std::vector<math::vector4>& out_colors, asset_diagnostic& diagnostic);

}
