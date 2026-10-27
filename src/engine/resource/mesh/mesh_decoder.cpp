// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "mesh_decoder.hpp"
#include "engine/resource/detail/asset_json.hpp"
#include "mesh_binary.hpp"
namespace gneiss::engine::detail {
// NOLINTNEXTLINE(readability-function-cognitive-complexity): 保持 Mesh Schema 诊断顺序。
[[nodiscard]] gneiss::engine::result
parse_mesh(const std::vector<std::byte>& bytes,
           std::vector<gneiss::engine::mesh_vertex>& out_vertices,
           std::vector<gneiss::engine::mesh_normal>& out_normals, asset_diagnostic& diagnostic) {
  auto document = parse_document(bytes, diagnostic);
  if (!document) {
    return diagnostic.result;
  }
  yyjson_val* root = yyjson_doc_get_root(document.get());
  yyjson_val* version_value = yyjson_obj_get(root, "version");
  const auto requested_version =
      yyjson_is_uint(version_value) ? yyjson_get_uint(version_value) : 0U;
  constexpr std::array v1_fields{std::string_view{"format"}, std::string_view{"version"},
                                 std::string_view{"topology"}, std::string_view{"vertices"}};
  constexpr std::array v2_fields{std::string_view{"format"}, std::string_view{"version"},
                                 std::string_view{"topology"}, std::string_view{"vertices"},
                                 std::string_view{"uvs"}};
  constexpr std::array v3_fields{std::string_view{"format"},   std::string_view{"version"},
                                 std::string_view{"topology"}, std::string_view{"vertices"},
                                 std::string_view{"uvs"},      std::string_view{"normals"}};
  std::uint64_t version = 0;
  auto fields = std::span<const std::string_view>{v1_fields};
  if (requested_version == 2U) {
    fields = v2_fields;
  } else if (requested_version == 3U) {
    fields = v3_fields;
  }
  if (!validate_header(root, "gneiss.mesh", fields, 3U, version, diagnostic)) {
    return diagnostic.result;
  }
  yyjson_val* topology = yyjson_obj_get(root, "topology");
  if (!yyjson_is_str(topology) || json_string(topology) != "triangle_list") {
    fail(diagnostic, gneiss::engine::result::invalid_argument, "/topology", "只支持 triangle_list");
    return diagnostic.result;
  }
  yyjson_val* vertices = yyjson_obj_get(root, "vertices");
  const auto count = yyjson_arr_size(vertices);
  if (!yyjson_is_arr(vertices) || count < 3U || count % 3U != 0U ||
      count > std::numeric_limits<std::uint32_t>::max()) {
    fail(diagnostic, gneiss::engine::result::invalid_argument, "/vertices",
         "顶点数必须是大于零的三角形列表");
    return diagnostic.result;
  }
  yyjson_val* uvs = yyjson_obj_get(root, "uvs");
  if (version >= 2U && (!yyjson_is_arr(uvs) || yyjson_arr_size(uvs) != count)) {
    fail(diagnostic, gneiss::engine::result::invalid_argument, "/uvs", "UV 数量必须与顶点数量一致");
    return diagnostic.result;
  }
  yyjson_val* normals = yyjson_obj_get(root, "normals");
  if (version == 3U && (!yyjson_is_arr(normals) || yyjson_arr_size(normals) != count)) {
    fail(diagnostic, gneiss::engine::result::invalid_argument, "/normals",
         "法线数量必须与顶点数量一致");
    return diagnostic.result;
  }
  out_vertices.reserve(count);
  out_normals.reserve(version == 3U ? count : 0U);
  std::size_t index = 0;
  std::size_t maximum = 0;
  yyjson_val* vertex = nullptr;
  yyjson_arr_foreach(vertices, index, maximum, vertex) {
    if (!yyjson_is_arr(vertex) || yyjson_arr_size(vertex) != 3U) {
      fail(diagnostic, gneiss::engine::result::invalid_argument,
           "/vertices/" + std::to_string(index), "顶点必须包含三个数值");
      return diagnostic.result;
    }
    gneiss::engine::mesh_vertex parsed{};
    if (!read_float(yyjson_arr_get(vertex, 0), parsed.x) ||
        !read_float(yyjson_arr_get(vertex, 1), parsed.y) ||
        !read_float(yyjson_arr_get(vertex, 2), parsed.z)) {
      fail(diagnostic, gneiss::engine::result::invalid_argument,
           "/vertices/" + std::to_string(index), "顶点数值必须是有限 float");
      return diagnostic.result;
    }
    if (version >= 2U) {
      yyjson_val* uv = yyjson_arr_get(uvs, index);
      if (!yyjson_is_arr(uv) || yyjson_arr_size(uv) != 2U ||
          !read_float(yyjson_arr_get(uv, 0), parsed.u) ||
          !read_float(yyjson_arr_get(uv, 1), parsed.v)) {
        fail(diagnostic, gneiss::engine::result::invalid_argument, "/uvs/" + std::to_string(index),
             "UV 必须包含两个有限 float 数值");
        return diagnostic.result;
      }
    }
    out_vertices.push_back(parsed);
    if (version >= 3U) {
      yyjson_val* normal = yyjson_arr_get(normals, index);
      gneiss::engine::mesh_normal parsed_normal{};
      if (!yyjson_is_arr(normal) || yyjson_arr_size(normal) != 3U ||
          !read_float(yyjson_arr_get(normal, 0), parsed_normal.x) ||
          !read_float(yyjson_arr_get(normal, 1), parsed_normal.y) ||
          !read_float(yyjson_arr_get(normal, 2), parsed_normal.z)) {
        fail(diagnostic, gneiss::engine::result::invalid_argument,
             "/normals/" + std::to_string(index), "法线必须包含三个有限数值");
        return diagnostic.result;
      }
      const auto length =
          std::sqrt((parsed_normal.x * parsed_normal.x) + (parsed_normal.y * parsed_normal.y) +
                    (parsed_normal.z * parsed_normal.z));
      if (std::abs(length - 1.0F) > 1.0e-4F) {
        fail(diagnostic, gneiss::engine::result::invalid_argument,
             "/normals/" + std::to_string(index), "法线必须归一化");
        return diagnostic.result;
      }
      out_normals.push_back(parsed_normal);
    }
  }
  return gneiss::engine::result::success;
}

[[nodiscard]] gneiss::engine::result parse_binary_mesh(
    const std::vector<std::byte>& bytes, std::vector<gneiss::engine::mesh_vertex>& out_vertices,
    std::vector<gneiss::engine::mesh_normal>& out_normals, std::vector<std::uint32_t>& out_indices,
    std::vector<gneiss::engine::mesh_tangent>& out_tangents,
    std::vector<gneiss::engine::mesh_uv>& out_uv1,
    std::vector<gneiss::engine::mesh_color>& out_colors, asset_diagnostic& diagnostic) {
  gneiss::engine::mesh_binary_data data;
  gneiss::engine::mesh_binary_diagnostic binary_diagnostic;
  const auto result = gneiss::engine::decode_mesh_binary(bytes, data, binary_diagnostic);
  if (result != gneiss::engine::mesh_binary_result::success) {
    fail(diagnostic,
         result == gneiss::engine::mesh_binary_result::unsupported_version
             ? gneiss::engine::result::unsupported
             : gneiss::engine::result::invalid_argument,
         "", binary_diagnostic.message, binary_diagnostic.byte_offset);
    return diagnostic.result;
  }
  out_vertices.reserve(data.vertices.size());
  out_normals.reserve(data.vertices.size());
  for (const auto& source : data.vertices) {
    out_vertices.push_back({.x = source.position[0],
                            .y = source.position[1],
                            .z = source.position[2],
                            .u = source.texcoord[0],
                            .v = source.texcoord[1]});
    out_normals.push_back({.x = source.normal[0], .y = source.normal[1], .z = source.normal[2]});
  }
  out_tangents.reserve(data.tangents.size());
  for (const auto& tangent : data.tangents) {
    out_tangents.push_back({.x = tangent[0], .y = tangent[1], .z = tangent[2], .w = tangent[3]});
  }
  for (const auto& uv : data.uv1) {
    out_uv1.push_back({.u = uv[0], .v = uv[1]});
  }
  for (const auto& c : data.colors) {
    out_colors.push_back({.r = c[0], .g = c[1], .b = c[2], .a = c[3]});
  }
  out_indices = std::move(data.indices);
  return gneiss::engine::result::success;
}

}
