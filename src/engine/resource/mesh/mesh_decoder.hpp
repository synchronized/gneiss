// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/resource/asset_diagnostic.hpp"
#include "engine/resource/mesh/mesh_data.hpp"
namespace gneiss::engine::detail {
[[nodiscard]] gneiss::engine::result
parse_mesh(const std::vector<std::byte>& bytes,
           std::vector<gneiss::engine::mesh_vertex>& out_vertices,
           std::vector<gneiss::engine::mesh_normal>& out_normals, asset_diagnostic& diagnostic);

[[nodiscard]] gneiss::engine::result parse_binary_mesh(
    const std::vector<std::byte>& bytes, std::vector<gneiss::engine::mesh_vertex>& out_vertices,
    std::vector<gneiss::engine::mesh_normal>& out_normals, std::vector<std::uint32_t>& out_indices,
    std::vector<gneiss::engine::mesh_tangent>& out_tangents,
    std::vector<gneiss::engine::mesh_uv>& out_uv1,
    std::vector<gneiss::engine::mesh_color>& out_colors, asset_diagnostic& diagnostic);

}
