// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/resource/mesh/mesh_decoder.hpp>
#include <gneiss/engine/resource/asset_file.hpp>
#include <gneiss/engine/resource/mesh/mesh_binary.hpp>
#include <gneiss/engine/resource/mesh/mesh_description.hpp>
namespace gneiss::detail {
[[nodiscard]] result<mesh_data, asset_diagnostic> parse_mesh(const std::vector<std::byte>& bytes) {
  auto decoded = asset_file::deserialize<mesh_description>(bytes);
  if (!decoded) {
    auto diagnostic = std::move(decoded).error();
    return asset_diagnostic{diagnostic.error, diagnostic.byte_offset, std::move(diagnostic.path),
                            std::move(diagnostic.message)};
  }
  mesh_data output;
  output.vertices.reserve(decoded->vertices.size());
  for (std::size_t i = 0; i < decoded->vertices.size(); ++i)
    output.vertices.push_back({decoded->vertices[i], decoded->uvs[i]});
  output.normals = std::move(decoded->normals);
  return output;
}

[[nodiscard]] result<mesh_data, asset_diagnostic>
parse_binary_mesh(const std::vector<std::byte>& bytes) {
  mesh_data output;
  auto& out_vertices = output.vertices;
  auto& out_normals = output.normals;
  auto& out_indices = output.indices;
  auto& out_tangents = output.tangents;
  auto& out_uv1 = output.uv1;
  auto& out_colors = output.colors;
  asset_diagnostic diagnostic;

  auto parsed = decode_mesh_binary(bytes);
  if (!parsed) {
    fail(diagnostic,
         parsed.error().error == mesh_binary_error::out_of_memory         ? error::out_of_memory
         : parsed.error().error == mesh_binary_error::unsupported_version ? error::unsupported
                                                                          : error::invalid_argument,
         "", parsed.error().message, parsed.error().byte_offset);
    return diagnostic;
  }
  auto data = std::move(*parsed);
  out_vertices.reserve(data.vertices.size());
  out_normals.reserve(data.vertices.size());
  for (const auto& source : data.vertices) {
    out_vertices.push_back(
        {.position = {source.position[0], source.position[1], source.position[2]},
         .uv = {source.texcoord[0], source.texcoord[1]}});
    out_normals.push_back({source.normal[0], source.normal[1], source.normal[2]});
  }
  out_tangents.reserve(data.tangents.size());
  for (const auto& tangent : data.tangents) {
    out_tangents.push_back({tangent[0], tangent[1], tangent[2], tangent[3]});
  }
  for (const auto& uv : data.uv1) {
    out_uv1.push_back({uv[0], uv[1]});
  }
  for (const auto& c : data.colors) {
    out_colors.push_back({c[0], c[1], c[2], c[3]});
  }
  out_indices = std::move(data.indices);
  return output;
}
}
