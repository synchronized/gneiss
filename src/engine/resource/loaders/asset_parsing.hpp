// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/result.hpp"
#include "engine/resource/loaders/asset_preparation.hpp"
#include "engine/resource/types/material_description.hpp"

// 同步加载与后台准备共用解析契约；不接触缓存或资源服务。
namespace gneiss::engine::detail {

void fail(asset_diagnostic& diagnostic, gneiss::engine::result result, std::string_view path,
          std::string_view message, std::size_t offset = 0) noexcept;

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

[[nodiscard]] gneiss::engine::result parse_material(const std::vector<std::byte>& bytes,
                                                    material_description& out_source,
                                                    asset_diagnostic& diagnostic);

} // namespace gneiss::engine::detail
