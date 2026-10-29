// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "mesh_decoder.hpp"
#include "engine/resource/detail/asset_json.hpp"
#include "mesh_binary.hpp"
namespace gneiss::engine::detail {
// NOLINTNEXTLINE(readability-function-cognitive-complexity): 保持 Mesh Schema 诊断顺序。
[[nodiscard]] result<mesh_data, asset_diagnostic>
parse_mesh(const std::vector<std::byte>& bytes) try {
  mesh_data output;
  auto& out_vertices = output.vertices;
  auto& out_normals = output.normals;
  asset_diagnostic diagnostic;

  auto parsed_document = parse_document(bytes);
  if (!parsed_document) {
    diagnostic = std::move(parsed_document).error();
    return diagnostic;
  }
  auto document = std::move(*parsed_document);
  yyjson_val* root = yyjson_doc_get_root(document.get());
  constexpr std::array fields{std::string_view{"format"},   std::string_view{"version"},
                              std::string_view{"topology"}, std::string_view{"vertices"},
                              std::string_view{"uvs"},      std::string_view{"normals"}};
  std::uint64_t version = 0;
  if (!validate_header(root, "gneiss.mesh", fields, 3U, version, diagnostic)) {
    return diagnostic;
  }
  if (version != 3U) {
    fail(diagnostic, error::unsupported, "/version", "网格 JSON 仅支持版本 3");
    return diagnostic;
  }
  yyjson_val* topology = yyjson_obj_get(root, "topology");
  if (!yyjson_is_str(topology) || json_string(topology) != "triangle_list") {
    fail(diagnostic, error::invalid_argument, "/topology", "只支持 triangle_list");
    return diagnostic;
  }
  yyjson_val* vertices = yyjson_obj_get(root, "vertices");
  const auto count = yyjson_arr_size(vertices);
  if (!yyjson_is_arr(vertices) || count < 3U || count % 3U != 0U ||
      count > std::numeric_limits<std::uint32_t>::max()) {
    fail(diagnostic, error::invalid_argument, "/vertices", "顶点数必须是大于零的三角形列表");
    return diagnostic;
  }
  yyjson_val* uvs = yyjson_obj_get(root, "uvs");
  if (!yyjson_is_arr(uvs) || yyjson_arr_size(uvs) != count) {
    fail(diagnostic, error::invalid_argument, "/uvs", "UV 数量必须与顶点数量一致");
    return diagnostic;
  }
  yyjson_val* normals = yyjson_obj_get(root, "normals");
  if (!yyjson_is_arr(normals) || yyjson_arr_size(normals) != count) {
    fail(diagnostic, error::invalid_argument, "/normals", "法线数量必须与顶点数量一致");
    return diagnostic;
  }
  out_vertices.reserve(count);
  out_normals.reserve(count);
  std::size_t index = 0;
  std::size_t maximum = 0;
  yyjson_val* vertex = nullptr;
  yyjson_arr_foreach(vertices, index, maximum, vertex) {
    if (!yyjson_is_arr(vertex) || yyjson_arr_size(vertex) != 3U) {
      fail(diagnostic, error::invalid_argument, "/vertices/" + std::to_string(index),
           "顶点必须包含三个数值");
      return diagnostic;
    }
    mesh_vertex parsed{};
    if (!read_float(yyjson_arr_get(vertex, 0), parsed.position.x) ||
        !read_float(yyjson_arr_get(vertex, 1), parsed.position.y) ||
        !read_float(yyjson_arr_get(vertex, 2), parsed.position.z)) {
      fail(diagnostic, error::invalid_argument, "/vertices/" + std::to_string(index),
           "顶点数值必须是有限 float");
      return diagnostic;
    }
    yyjson_val* uv = yyjson_arr_get(uvs, index);
    if (!yyjson_is_arr(uv) || yyjson_arr_size(uv) != 2U ||
        !read_float(yyjson_arr_get(uv, 0), parsed.uv.x) ||
        !read_float(yyjson_arr_get(uv, 1), parsed.uv.y)) {
      fail(diagnostic, error::invalid_argument, "/uvs/" + std::to_string(index),
           "UV 必须包含两个有限 float 数值");
      return diagnostic;
    }
    out_vertices.push_back(parsed);
    yyjson_val* normal = yyjson_arr_get(normals, index);
    math::vector3 parsed_normal{};
    if (!yyjson_is_arr(normal) || yyjson_arr_size(normal) != 3U ||
        !read_float(yyjson_arr_get(normal, 0), parsed_normal.x) ||
        !read_float(yyjson_arr_get(normal, 1), parsed_normal.y) ||
        !read_float(yyjson_arr_get(normal, 2), parsed_normal.z)) {
      fail(diagnostic, error::invalid_argument, "/normals/" + std::to_string(index),
           "法线必须包含三个有限数值");
      return diagnostic;
    }
    const auto length =
        std::sqrt((parsed_normal.x * parsed_normal.x) + (parsed_normal.y * parsed_normal.y) +
                  (parsed_normal.z * parsed_normal.z));
    if (std::abs(length - 1.0F) > 1.0e-4F) {
      fail(diagnostic, error::invalid_argument, "/normals/" + std::to_string(index),
           "法线必须归一化");
      return diagnostic;
    }
    out_normals.push_back(parsed_normal);
  }
  return output;

} catch (const std::bad_alloc&) {
  return asset_diagnostic{.error = error::out_of_memory};
} catch (...) {
  return asset_diagnostic{.error = error::internal};
}

[[nodiscard]] result<mesh_data, asset_diagnostic>
parse_binary_mesh(const std::vector<std::byte>& bytes) try {
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

} catch (const std::bad_alloc&) {
  return asset_diagnostic{.error = error::out_of_memory};
} catch (...) {
  return asset_diagnostic{.error = error::internal};
}
}
