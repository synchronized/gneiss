// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "render/render_asset_preparation.hpp"

// 同步加载与后台准备共用解析契约；不接触缓存或资源服务。
namespace gneiss::render_internal::asset_parsing {

struct material_source final {
  asset_internal::material_parameters parameters;
  std::array<std::string, 5> texture_uris;
};

void fail(asset_diagnostic& diagnostic, gneiss_result result, std::string_view path,
          std::string_view message, std::size_t offset = 0) noexcept;

[[nodiscard]] gneiss_result parse_mesh(const std::vector<std::byte>& bytes,
                                       std::vector<gneiss_mesh_vertex>& out_vertices,
                                       std::vector<gneiss_mesh_normal>& out_normals,
                                       asset_diagnostic& diagnostic);

[[nodiscard]] gneiss_result parse_binary_mesh(
    const std::vector<std::byte>& bytes, std::vector<gneiss_mesh_vertex>& out_vertices,
    std::vector<gneiss_mesh_normal>& out_normals, std::vector<std::uint32_t>& out_indices,
    std::vector<gneiss_mesh_tangent>& out_tangents, std::vector<gneiss_mesh_uv>& out_uv1,
    std::vector<gneiss_mesh_color>& out_colors, asset_diagnostic& diagnostic);

[[nodiscard]] gneiss_result parse_material(const std::vector<std::byte>& bytes,
                                           material_source& out_source,
                                           asset_diagnostic& diagnostic);

} // namespace gneiss::render_internal::asset_parsing
