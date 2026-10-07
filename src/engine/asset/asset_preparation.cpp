// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/asset/asset_preparation.hpp"

#include "engine/asset/asset_parsing.hpp"

#include "engine/asset/mesh_binary.hpp"
#include "engine/asset/png_decoder.hpp"
#include "engine/asset/read_slice.hpp"
#include "engine/asset/source_revision_file_system.hpp"
#include "engine/asset/texture_binary.hpp"
#include "engine/asset/texture_container.hpp"
#include "engine/asset/texture_ktx2.hpp"
#include "engine/asset/virtual_file_system.hpp"

#include <yyjson.h>

#if defined(GNEISS_HAS_GRANIT_PLATFORM)
#include <granit/renderer/texture_asset.hpp>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <new>
#include <span>
#include <vector>

namespace gneiss::asset_internal::asset_parsing {

struct texture_source final {
  std::string uri;
  std::uint32_t color_space{};
};

void fail(asset_diagnostic& diagnostic, gneiss_result result, std::string_view path,
          std::string_view message, std::size_t offset) noexcept {
  diagnostic.result = result;
  diagnostic.byte_offset = offset;
  try {
    diagnostic.path = path;
    diagnostic.message = message;
  } catch (...) {
    diagnostic.result = GNEISS_ERROR_OUT_OF_MEMORY;
    diagnostic.path.clear();
    diagnostic.message.clear();
  }
}

[[nodiscard]] std::string_view json_string(yyjson_val* value) {
  return {yyjson_get_str(value), yyjson_get_len(value)};
}

[[nodiscard]] bool has_only_fields(yyjson_val* object, std::span<const std::string_view> fields,
                                   asset_diagnostic& diagnostic) {
  yyjson_val* key = nullptr;
  yyjson_val* value = nullptr;
  std::size_t index = 0;
  std::size_t maximum = 0;
  yyjson_obj_foreach(object, index, maximum, key, value) {
    (void)value;
    const auto name = json_string(key);
    if (std::ranges::find(fields, name) == fields.end()) {
      fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "/" + std::string(name), "未知字段");
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool validate_header(yyjson_val* root, std::string_view expected_format,
                                   std::span<const std::string_view> fields,
                                   std::uint64_t maximum_version, std::uint64_t& out_version,
                                   asset_diagnostic& diagnostic) {
  if (!yyjson_is_obj(root) || !has_only_fields(root, fields, diagnostic)) {
    if (diagnostic.result == GNEISS_SUCCESS) {
      fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "", "资产根必须是对象");
    }
    return false;
  }
  yyjson_val* format = yyjson_obj_get(root, "format");
  if (!yyjson_is_str(format) || json_string(format) != expected_format) {
    fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "/format", "资产格式标识错误");
    return false;
  }
  yyjson_val* version = yyjson_obj_get(root, "version");
  if (!yyjson_is_uint(version) || yyjson_get_uint(version) == 0U ||
      yyjson_get_uint(version) > maximum_version) {
    fail(diagnostic,
         yyjson_is_uint(version) && yyjson_get_uint(version) > 1U ? GNEISS_ERROR_UNSUPPORTED
                                                                  : GNEISS_ERROR_INVALID_ARGUMENT,
         "/version", "不支持的资产版本");
    return false;
  }
  out_version = yyjson_get_uint(version);
  return true;
}

using document_ptr = std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)>;

[[nodiscard]] document_ptr parse_document(const std::vector<std::byte>& bytes,
                                          asset_diagnostic& diagnostic) {
  if (bytes.empty()) {
    fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "", "资产文档为空");
    return {nullptr, &yyjson_doc_free};
  }
  yyjson_read_err error{};
  document_ptr document(
      yyjson_read_opts(reinterpret_cast<char*>(const_cast<std::byte*>(bytes.data())), bytes.size(),
                       YYJSON_READ_NOFLAG, nullptr, &error),
      &yyjson_doc_free);
  if (!document) {
    fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "",
         error.msg != nullptr ? error.msg : "JSON 语法错误", error.pos);
  }
  return {document.release(), &yyjson_doc_free};
}

[[nodiscard]] bool read_float(yyjson_val* value, float& output) {
  const double number = yyjson_get_num(value);
  if (!yyjson_is_num(value) || !std::isfinite(number) ||
      std::abs(number) > std::numeric_limits<float>::max()) {
    return false;
  }
  output = static_cast<float>(number);
  return true;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): 保持 Mesh Schema 诊断顺序。
[[nodiscard]] gneiss_result parse_mesh(const std::vector<std::byte>& bytes,
                                       std::vector<gneiss_mesh_vertex>& out_vertices,
                                       std::vector<gneiss_mesh_normal>& out_normals,
                                       asset_diagnostic& diagnostic) {
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
    fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "/topology", "只支持 triangle_list");
    return diagnostic.result;
  }
  yyjson_val* vertices = yyjson_obj_get(root, "vertices");
  const auto count = yyjson_arr_size(vertices);
  if (!yyjson_is_arr(vertices) || count < 3U || count % 3U != 0U ||
      count > std::numeric_limits<std::uint32_t>::max()) {
    fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "/vertices", "顶点数必须是大于零的三角形列表");
    return diagnostic.result;
  }
  yyjson_val* uvs = yyjson_obj_get(root, "uvs");
  if (version >= 2U && (!yyjson_is_arr(uvs) || yyjson_arr_size(uvs) != count)) {
    fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "/uvs", "UV 数量必须与顶点数量一致");
    return diagnostic.result;
  }
  yyjson_val* normals = yyjson_obj_get(root, "normals");
  if (version == 3U && (!yyjson_is_arr(normals) || yyjson_arr_size(normals) != count)) {
    fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "/normals", "法线数量必须与顶点数量一致");
    return diagnostic.result;
  }
  out_vertices.reserve(count);
  out_normals.reserve(version == 3U ? count : 0U);
  std::size_t index = 0;
  std::size_t maximum = 0;
  yyjson_val* vertex = nullptr;
  yyjson_arr_foreach(vertices, index, maximum, vertex) {
    if (!yyjson_is_arr(vertex) || yyjson_arr_size(vertex) != 3U) {
      fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "/vertices/" + std::to_string(index),
           "顶点必须包含三个数值");
      return diagnostic.result;
    }
    gneiss_mesh_vertex parsed{};
    if (!read_float(yyjson_arr_get(vertex, 0), parsed.x) ||
        !read_float(yyjson_arr_get(vertex, 1), parsed.y) ||
        !read_float(yyjson_arr_get(vertex, 2), parsed.z)) {
      fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "/vertices/" + std::to_string(index),
           "顶点数值必须是有限 float");
      return diagnostic.result;
    }
    if (version >= 2U) {
      yyjson_val* uv = yyjson_arr_get(uvs, index);
      if (!yyjson_is_arr(uv) || yyjson_arr_size(uv) != 2U ||
          !read_float(yyjson_arr_get(uv, 0), parsed.u) ||
          !read_float(yyjson_arr_get(uv, 1), parsed.v)) {
        fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "/uvs/" + std::to_string(index),
             "UV 必须包含两个有限 float 数值");
        return diagnostic.result;
      }
    }
    out_vertices.push_back(parsed);
    if (version >= 3U) {
      yyjson_val* normal = yyjson_arr_get(normals, index);
      gneiss_mesh_normal parsed_normal{};
      if (!yyjson_is_arr(normal) || yyjson_arr_size(normal) != 3U ||
          !read_float(yyjson_arr_get(normal, 0), parsed_normal.x) ||
          !read_float(yyjson_arr_get(normal, 1), parsed_normal.y) ||
          !read_float(yyjson_arr_get(normal, 2), parsed_normal.z)) {
        fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "/normals/" + std::to_string(index),
             "法线必须包含三个有限数值");
        return diagnostic.result;
      }
      const auto length =
          std::sqrt((parsed_normal.x * parsed_normal.x) + (parsed_normal.y * parsed_normal.y) +
                    (parsed_normal.z * parsed_normal.z));
      if (std::abs(length - 1.0F) > 1.0e-4F) {
        fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "/normals/" + std::to_string(index),
             "法线必须归一化");
        return diagnostic.result;
      }
      out_normals.push_back(parsed_normal);
    }
  }
  return GNEISS_SUCCESS;
}

[[nodiscard]] gneiss_result parse_binary_mesh(
    const std::vector<std::byte>& bytes, std::vector<gneiss_mesh_vertex>& out_vertices,
    std::vector<gneiss_mesh_normal>& out_normals, std::vector<std::uint32_t>& out_indices,
    std::vector<gneiss_mesh_tangent>& out_tangents, std::vector<gneiss_mesh_uv>& out_uv1,
    std::vector<gneiss_mesh_color>& out_colors, asset_diagnostic& diagnostic) {
  gneiss::asset_internal::mesh_binary_data data;
  gneiss::asset_internal::mesh_binary_diagnostic binary_diagnostic;
  const auto result = gneiss::asset_internal::decode_mesh_binary(bytes, data, binary_diagnostic);
  if (result != gneiss::asset_internal::mesh_binary_result::success) {
    fail(diagnostic,
         result == gneiss::asset_internal::mesh_binary_result::unsupported_version
             ? GNEISS_ERROR_UNSUPPORTED
             : GNEISS_ERROR_INVALID_ARGUMENT,
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
  return GNEISS_SUCCESS;
}

[[nodiscard]] gneiss_result parse_pbr_extensions(yyjson_val* root, material_source& out_source,
                                                 asset_diagnostic& diagnostic) {
  constexpr std::array names{
      "base_color_texture", "metallic_roughness_texture", "normal_texture",
      "occlusion_texture",  "emissive_texture",
  };
  for (std::size_t slot = 1; slot < names.size(); ++slot) {
    auto* texture = yyjson_obj_get(root, names[slot]);
    if (texture == nullptr || yyjson_is_null(texture)) {
      continue;
    }
    if (!yyjson_is_str(texture) || yyjson_get_len(texture) == 0U) {
      fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, std::string{"/"} + names[slot],
           "Texture 必须是非空 URI 或 null");
      return diagnostic.result;
    }
    out_source.texture_uris[slot].assign(json_string(texture));
  }
  auto* normal_scale = yyjson_obj_get(root, "normal_scale");
  auto* strength = yyjson_obj_get(root, "occlusion_strength");
  if ((normal_scale != nullptr && !read_float(normal_scale, out_source.parameters.normal_scale)) ||
      (strength != nullptr && (!read_float(strength, out_source.parameters.occlusion_strength) ||
                               out_source.parameters.occlusion_strength < 0.0F ||
                               out_source.parameters.occlusion_strength > 1.0F))) {
    fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "/normal_scale",
         "法线缩放必须有限，AO 强度必须位于 0..1");
    return diagnostic.result;
  }
  if (auto* emissive = yyjson_obj_get(root, "emissive")) {
    if (!yyjson_is_arr(emissive) || yyjson_arr_size(emissive) != 3U) {
      fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "/emissive", "自发光必须包含三个分量");
      return diagnostic.result;
    }
    for (std::size_t i = 0; i < 3U; ++i) {
      if (!read_float(yyjson_arr_get(emissive, i), out_source.parameters.emissive[i]) ||
          out_source.parameters.emissive[i] < 0.0F || out_source.parameters.emissive[i] > 1.0F) {
        fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "/emissive", "自发光分量必须位于 0..1");
        return diagnostic.result;
      }
    }
  }
  return GNEISS_SUCCESS;
}

[[nodiscard]] bool parse_material_sampling(yyjson_val* root,
                                           asset_internal::material_parameters& state) {
  if (auto* sampling = yyjson_obj_get(root, "sampling")) {
    if (!yyjson_is_arr(sampling) || yyjson_arr_size(sampling) != 5U) {
      return false;
    }
    constexpr std::array names{
        "uv_set", "mag_filter", "min_filter", "mip_filter", "address_u", "address_v",
    };
    constexpr std::array limits{1U, 1U, 1U, 2U, 2U, 2U};
    for (std::size_t slot = 0; slot < 5U; ++slot) {
      auto* item = yyjson_arr_get(sampling, slot);
      if (!yyjson_is_obj(item) || yyjson_obj_size(item) != names.size()) {
        return false;
      }
      auto& sample = state.sampling[slot];
      const std::array destinations{
          &sample.uv_set,     &sample.mag_filter, &sample.min_filter,
          &sample.mip_filter, &sample.address_u,  &sample.address_v,
      };
      for (std::size_t field = 0; field < names.size(); ++field) {
        auto* value = yyjson_obj_get(item, names[field]);
        if (!yyjson_is_uint(value) || yyjson_get_uint(value) > limits[field]) {
          return false;
        }
        *destinations[field] = static_cast<std::uint32_t>(yyjson_get_uint(value));
      }
    }
  }
  return true;
}

[[nodiscard]] bool parse_material_state(yyjson_val* root,
                                        asset_internal::material_parameters& state) {
  if (auto* mode = yyjson_obj_get(root, "alpha_mode")) {
    if (!yyjson_is_str(mode)) {
      return false;
    }
    const auto name = json_string(mode);
    if (name == "OPAQUE") {
      state.alpha_mode = GNEISS_MATERIAL_ALPHA_OPAQUE;
    } else if (name == "MASK") {
      state.alpha_mode = GNEISS_MATERIAL_ALPHA_MASK;
    } else if (name == "BLEND") {
      state.alpha_mode = GNEISS_MATERIAL_ALPHA_BLEND;
    } else {
      return false;
    }
  }
  if (auto* sided = yyjson_obj_get(root, "double_sided")) {
    if (!yyjson_is_bool(sided)) {
      return false;
    }
    state.double_sided = yyjson_get_bool(sided) ? 1U : 0U;
  }
  if (auto* cutoff = yyjson_obj_get(root, "alpha_cutoff");
      cutoff && (!read_float(cutoff, state.alpha_cutoff) || state.alpha_cutoff < 0.0F)) {
    return false;
  }

  return parse_material_sampling(root, state);
}

[[nodiscard]] gneiss_result parse_material(const std::vector<std::byte>& bytes,
                                           material_source& out_source,
                                           asset_diagnostic& diagnostic) {
  auto document = parse_document(bytes, diagnostic);
  if (!document) {
    return diagnostic.result;
  }
  yyjson_val* root = yyjson_doc_get_root(document.get());
  yyjson_val* version_value = yyjson_obj_get(root, "version");
  const auto requested_version =
      yyjson_is_uint(version_value) ? yyjson_get_uint(version_value) : 0U;
  constexpr std::array v1_fields{std::string_view{"format"}, std::string_view{"version"},
                                 std::string_view{"color"}};
  constexpr std::array v2_fields{std::string_view{"format"}, std::string_view{"version"},
                                 std::string_view{"color"}, std::string_view{"base_color_texture"}};
  constexpr std::array v3_fields{
      std::string_view{"format"},   std::string_view{"version"},
      std::string_view{"color"},    std::string_view{"base_color_texture"},
      std::string_view{"metallic"}, std::string_view{"roughness"}};
  constexpr std::array v4_fields{
      std::string_view{"format"},
      std::string_view{"version"},
      std::string_view{"color"},
      std::string_view{"base_color_texture"},
      std::string_view{"metallic"},
      std::string_view{"roughness"},
      std::string_view{"metallic_roughness_texture"},
      std::string_view{"normal_texture"},
      std::string_view{"occlusion_texture"},
      std::string_view{"emissive_texture"},
      std::string_view{"normal_scale"},
      std::string_view{"occlusion_strength"},
      std::string_view{"emissive"},
  };
  auto v5_fields = std::vector<std::string_view>(v4_fields.begin(), v4_fields.end());
  for (const auto* const name : {"alpha_mode", "double_sided", "alpha_cutoff", "sampling"}) {
    v5_fields.emplace_back(name);
  }
  std::uint64_t version = 0;
  auto fields = std::span<const std::string_view>{v1_fields};
  switch (requested_version) {
  case 2U:
    fields = v2_fields;
    break;
  case 3U:
    fields = v3_fields;
    break;
  case 4U:
    fields = v4_fields;
    break;
  case 5U:
    fields = v5_fields;
    break;
  default:
    break;
  }
  if (!validate_header(root, "gneiss.material", fields, 5U, version, diagnostic)) {
    return diagnostic.result;
  }
  yyjson_val* color = yyjson_obj_get(root, "color");
  if (!yyjson_is_arr(color) || yyjson_arr_size(color) != out_source.parameters.color.size()) {
    fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "/color", "颜色必须包含四个分量");
    return diagnostic.result;
  }
  for (std::size_t index = 0; index < out_source.parameters.color.size(); ++index) {
    if (!read_float(yyjson_arr_get(color, index), out_source.parameters.color[index]) ||
        out_source.parameters.color[index] < 0.0F || out_source.parameters.color[index] > 1.0F) {
      fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "/color/" + std::to_string(index),
           "颜色分量必须位于 0..1");
      return diagnostic.result;
    }
  }
  if (version >= 2U) {
    yyjson_val* texture = yyjson_obj_get(root, "base_color_texture");
    if (version >= 3U && yyjson_is_null(texture)) {
      out_source.texture_uris[0].clear();
    } else if (!yyjson_is_str(texture) || yyjson_get_len(texture) == 0U) {
      fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "/base_color_texture",
           "base-color Texture 必须是非空 URI");
      return diagnostic.result;
    } else {
      out_source.texture_uris[0].assign(json_string(texture));
    }
  }
  if ((version >= 3U) &&
      (!read_float(yyjson_obj_get(root, "metallic"), out_source.parameters.metallic) ||
       out_source.parameters.metallic < 0.0F || out_source.parameters.metallic > 1.0F ||
       !read_float(yyjson_obj_get(root, "roughness"), out_source.parameters.roughness) ||
       out_source.parameters.roughness < 0.0F || out_source.parameters.roughness > 1.0F)) {
    fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "/metallic",
         "metallic 与 roughness 必须位于 0..1");
    return diagnostic.result;
  }

  if (version >= 4U && parse_pbr_extensions(root, out_source, diagnostic) != GNEISS_SUCCESS) {
    return diagnostic.result;
  }
  if (version == 5U && !parse_material_state(root, out_source.parameters)) {
    fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "/sampling", "材质状态或逐槽采样设置无效");
    return diagnostic.result;
  }
  return GNEISS_SUCCESS;
}

[[nodiscard]] gneiss_result parse_texture(const std::vector<std::byte>& bytes,
                                          texture_source& out_source,
                                          asset_diagnostic& diagnostic) {
  auto document = parse_document(bytes, diagnostic);
  if (!document) {
    return diagnostic.result;
  }
  yyjson_val* root = yyjson_doc_get_root(document.get());
  constexpr std::array v1_fields{
      std::string_view{"format"},
      std::string_view{"version"},
      std::string_view{"source"},
      std::string_view{"color_space"},
  };
  constexpr std::array v2_fields{
      std::string_view{"format"},      std::string_view{"version"}, std::string_view{"source"},
      std::string_view{"color_space"}, std::string_view{"usage"},
  };
  auto* const requested = yyjson_obj_get(root, "version");
  const auto fields = yyjson_is_uint(requested) && yyjson_get_uint(requested) == 2U
                          ? std::span<const std::string_view>{v2_fields}
                          : std::span<const std::string_view>{v1_fields};
  std::uint64_t version = 0;
  if (!validate_header(root, "gneiss.texture", fields, 2U, version, diagnostic)) {
    return diagnostic.result;
  }
  yyjson_val* source = yyjson_obj_get(root, "source");
  if (!yyjson_is_str(source) || yyjson_get_len(source) == 0U) {
    fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "/source", "Texture source 必须是非空 URI");
    return diagnostic.result;
  }
  yyjson_val* color_space = yyjson_obj_get(root, "color_space");
  if (!yyjson_is_str(color_space)) {
    fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "/color_space", "Texture 颜色空间必须是字符串");
    return diagnostic.result;
  }
  const auto color_space_name = json_string(color_space);
  if (color_space_name == "srgb") {
    out_source.color_space = GNEISS_TEXTURE_COLOR_SPACE_SRGB;
  } else if (color_space_name == "linear") {
    out_source.color_space = GNEISS_TEXTURE_COLOR_SPACE_LINEAR;
  } else {
    fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "/color_space", "只支持 srgb 或 linear");
    return diagnostic.result;
  }
  if (version == 2U) {
    auto* const usage = yyjson_obj_get(root, "usage");
    if (!yyjson_is_str(usage) || json_string(usage) != "normal" ||
        out_source.color_space != GNEISS_TEXTURE_COLOR_SPACE_LINEAR) {
      fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "/usage",
           "Texture v2 当前只支持 linear 法线用途");
      return diagnostic.result;
    }
  }
  out_source.uri.assign(json_string(source));
  return GNEISS_SUCCESS;
}

} // namespace gneiss::asset_internal::asset_parsing

namespace gneiss::asset_internal {
using namespace asset_parsing;

namespace {
bool invalid_prepared_mesh(const mesh_data& mesh) {
  return (mesh.vertices.size() < 3U ||
          (!mesh.normals.empty() && mesh.normals.size() != mesh.vertices.size()) ||
          (!mesh.indices.empty() && (mesh.indices.size() < 3U || mesh.indices.size() % 3U != 0U)) ||
          !std::ranges::all_of(mesh.vertices,
                               [](const auto& v) {
                                 return std::isfinite(v.x) && std::isfinite(v.y) &&
                                        std::isfinite(v.z) && std::isfinite(v.u) &&
                                        std::isfinite(v.v);
                               }) ||
          !std::ranges::all_of(mesh.normals,
                               [](const auto& n) {
                                 const auto length =
                                     std::sqrt((n.x * n.x) + (n.y * n.y) + (n.z * n.z));
                                 return std::isfinite(length) && std::abs(length - 1.0F) <= 1.0e-4F;
                               }) ||
          !std::ranges::all_of(mesh.indices, [&](auto i) { return i < mesh.vertices.size(); }));
}
gneiss_result decode_non_texture(const asset_request& source, const std::vector<std::byte>& bytes,
                                 prepared_asset& asset, asset_diagnostic& diagnostic,
                                 std::size_t material_bytes, std::size_t maximum_bytes,
                                 std::vector<asset_request>& pending) {
  auto result = GNEISS_SUCCESS;
  if (source.type == asset_type::mesh) {
    result =
        asset_internal::is_mesh_binary(bytes)
            ? parse_binary_mesh(bytes, asset.mesh.vertices, asset.mesh.normals, asset.mesh.indices,
                                asset.mesh.tangents, asset.mesh.uv1, asset.mesh.colors, diagnostic)
            : parse_mesh(bytes, asset.mesh.vertices, asset.mesh.normals, diagnostic);
    // 此校验与 RID 创建的顶点/法线/索引契约一致，在后台完成。
    const auto& mesh = asset.mesh;
    if (result == GNEISS_SUCCESS && invalid_prepared_mesh(mesh)) {
      result = GNEISS_ERROR_INVALID_ARGUMENT;
    }
    asset.bytes = mesh.data_bytes();
  } else if (source.type == asset_type::material) {
    material_source material;
    result = parse_material(bytes, material, diagnostic);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    asset.material = material.parameters;

    asset.texture_uris = std::move(material.texture_uris);
    // 由调用方传入发布预算，Asset 不依赖资源对象布局。
    if (material_bytes == 0U) {
      fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, source.uri, "材质发布预算不得为零");
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    if (material_bytes > maximum_bytes) {
      fail(diagnostic, GNEISS_ERROR_OUT_OF_MEMORY, source.uri, "材质发布预算超过批次上限");
      return GNEISS_ERROR_OUT_OF_MEMORY;
    }
    asset.bytes = material_bytes;
    for (const auto& uri : asset.texture_uris) {
      if (result == GNEISS_SUCCESS && !uri.empty()) {
        pending.push_back({.uri = uri, .type = asset_type::texture});
      }
      if (uri.size() > maximum_bytes - asset.bytes) {
        fail(diagnostic, GNEISS_ERROR_OUT_OF_MEMORY, source.uri, "材质依赖超出批次字节上限");
        return GNEISS_ERROR_OUT_OF_MEMORY;
      }
      asset.bytes += uri.size();
    }
  } else {
    result = GNEISS_ERROR_INVALID_ARGUMENT;
  }
  return result;
}
class shared_read_source final : public read_source {
public:
  explicit shared_read_source(std::shared_ptr<read_source> source) : source_(std::move(source)) {}
  [[nodiscard]] gneiss_result
  begin_read(std::uint64_t offset, std::size_t size,
             std::unique_ptr<read_operation>& output) const noexcept override {
    return source_->begin_read(offset, size, output);
  }
  [[nodiscard]] std::uint64_t size() const noexcept override { return source_->size(); }
  [[nodiscard]] gneiss_result read_at(std::uint64_t offset,
                                      std::span<std::byte> output) const noexcept override {
    return source_->read_at(offset, output);
  }

private:
  std::shared_ptr<read_source> source_;
};
// 单项解析只访问已验证的输入，不在同步解析调用内重新触发整文件扫描。
class prepared_input_files final : public file_system {
public:
  std::string description_uri;
  std::string image_uri;
  std::vector<std::byte> description;
  std::vector<std::byte> image;
  std::shared_ptr<read_source> source;
  gneiss_result read(std::string_view path,
                     std::vector<std::byte>& output) const noexcept override {
    return read_bounded(path, std::numeric_limits<std::size_t>::max(), output);
  }
  gneiss_result read_bounded(std::string_view path, std::size_t limit,
                             std::vector<std::byte>& output) const noexcept override {
    output.clear();
    try {
      const auto uri = "asset://" + std::string(path);
      const std::vector<std::byte>* bytes = nullptr;
      if (uri == description_uri) {
        bytes = &description;
      } else if (uri == image_uri) {
        bytes = &image;
      }
      if (bytes == nullptr) {
        return GNEISS_ERROR_NOT_FOUND;
      }
      if (bytes->size() > limit) {
        return GNEISS_ERROR_INVALID_ARGUMENT;
      }
      output = *bytes;
      return GNEISS_SUCCESS;
    } catch (...) {
      return GNEISS_ERROR_OUT_OF_MEMORY;
    }
  }
  gneiss_result open_read(std::string_view path,
                          std::unique_ptr<read_source>& output) const noexcept override {
    output.reset();
    try {
      if ("asset://" + std::string(path) != image_uri || !source) {
        return GNEISS_ERROR_UNSUPPORTED;
      }
      output = std::make_unique<shared_read_source>(source);
      return GNEISS_SUCCESS;
    } catch (...) {
      return GNEISS_ERROR_OUT_OF_MEMORY;
    }
  }
};
unsigned asset_order(asset_type type) {
  switch (type) {
  case asset_type::texture:
    return 0U;
  case asset_type::mesh:
    return 1U;
  default:
    return 2U;
  }
}
} // namespace

struct asset_preparation::state {
  enum class phase : std::uint8_t { select, description, payload, decode, verify, finished };
  virtual_file_system files;
  std::shared_ptr<source_revision_file_system> versions;
  std::unique_ptr<source_revision_file_system::verification> verification;
  std::vector<asset_request> pending;
  std::map<std::string, asset_type> seen;
  std::size_t index{};
  std::size_t material_bytes{};
  std::size_t maximum_assets{};
  std::size_t maximum_bytes{};
  texture_prepare_profile profile;
  prepared_batch batch;
  prepared_asset current;
  std::shared_ptr<prepared_input_files> input;
  std::unique_ptr<read_source> reader;
  read_slice slice;
  struct prefetched_input {
    std::unique_ptr<read_source> source;
    std::unique_ptr<read_operation> operation;
  };
  // 仅预读当前批次的短 Mesh 描述，最多 8 MiB；不提前解码或发布候选。
  std::map<std::string, prefetched_input> prefetched;
  std::vector<std::byte> bytes;
  core::sha256_builder digest;
  std::uint64_t offset{};
  bool retain{};
  bool read_complete{};
  bool selected{};
  bool deferred_reads{true};
  phase step{phase::select};
  gneiss_result result{GNEISS_SUCCESS};
  asset_diagnostic diagnostic;

  void prefetch_descriptions() {
    if (!deferred_reads) {
      return;
    }
    for (auto next = index; next < pending.size() && next - index < 8U; ++next) {
      const auto& requested = pending[next];
      if (prefetched.size() >= 8U) {
        break;
      }
      if (requested.type != asset_type::mesh || seen.contains(requested.uri) ||
          prefetched.contains(requested.uri)) {
        continue;
      }
      prefetched_input ahead;
      if (files.open_read_for_validation(requested.uri, ahead.source) != GNEISS_SUCCESS ||
          ahead.source->size() > std::size_t{1024U} * 1024U ||
          ahead.source->size() > maximum_bytes) {
        continue;
      }
      const auto started = ahead.source->begin_read(
          0U, static_cast<std::size_t>(ahead.source->size()), ahead.operation);
      if (started == GNEISS_ERROR_UNSUPPORTED) {
        return;
      }
      if (started == GNEISS_SUCCESS) {
        prefetched.emplace(requested.uri, std::move(ahead));
      }
    }
  }

  gneiss_result begin_read(std::string_view uri, std::size_t limit, bool keep_bytes) {
    slice = {};
    reader.reset();
    bytes = {};
    digest = {};
    offset = 0U;
    retain = keep_bytes;
    read_complete = false;
    auto opened = GNEISS_SUCCESS;
    const auto found = prefetched.find(std::string(uri));
    if (found == prefetched.end()) {
      opened = files.open_read_for_validation(uri, reader);
    } else {
      reader = std::move(found->second.source);
      slice.prime(std::move(found->second.operation),
                  {.offset = 0U, .size = static_cast<std::size_t>(reader->size())});
      prefetched.erase(found);
    }
    if (opened == GNEISS_ERROR_UNSUPPORTED && retain) {
      // 兼容仅提供整文件读取的自定义后端；不宣称这一回退具有分块时间上界。
      const auto loaded = files.read_bounded(uri, limit, bytes);
      read_complete = loaded == GNEISS_SUCCESS;
      return loaded;
    }
    if (opened != GNEISS_SUCCESS) {
      return opened;
    }
    if (retain) {
      if (reader->size() > limit) {
        return GNEISS_ERROR_INVALID_ARGUMENT;
      }
      bytes.resize(static_cast<std::size_t>(reader->size()));
    }
    return GNEISS_SUCCESS;
  }
  gneiss_result read_step(std::size_t& budget, const std::function<bool()>& cancelled) {
    if (read_complete) {
      return GNEISS_SUCCESS;
    }
    while (offset < reader->size() && budget != 0U) {
      if (cancelled && cancelled()) {
        return GNEISS_ERROR_INVALID_STATE;
      }
      const auto count =
          static_cast<std::size_t>(std::min<std::uint64_t>(budget, reader->size() - offset));
      std::span<const std::byte> chunk;
      const auto loaded =
          slice.take(*reader, {.offset = offset, .size = count}, chunk, deferred_reads);
      if (loaded == GNEISS_ERROR_NOT_READY) {
        budget = 0U;
        return GNEISS_SUCCESS;
      }
      if (loaded != GNEISS_SUCCESS) {
        return loaded;
      }
      if (retain) {
        std::ranges::copy(chunk, bytes.begin() + static_cast<std::ptrdiff_t>(offset));
      }
      digest.update(chunk);
      offset += chunk.size();
      budget -= chunk.size();
    }
    if (offset == reader->size()) {
      if (cancelled && cancelled()) {
        return GNEISS_ERROR_INVALID_STATE;
      }
      const auto checked = reader->complete_validation(digest.digest());
      if (checked != GNEISS_SUCCESS) {
        return checked;
      }
      read_complete = true;
    }
    return GNEISS_SUCCESS;
  }
  gneiss_result select() {
    prefetch_descriptions();
    for (unsigned count = 0U; index < pending.size() && count < 16U; ++count) {
      const auto source = pending[index++];
      if (const auto previous = seen.find(source.uri); previous != seen.end()) {
        if (previous->second != source.type) {
          return GNEISS_ERROR_INVALID_ARGUMENT;
        }
        continue;
      }
      if (seen.size() == maximum_assets) {
        return GNEISS_ERROR_INVALID_ARGUMENT;
      }
      seen.emplace(source.uri, source.type);
      current = {};
      current.source = source;
      input = std::make_shared<prepared_input_files>();
      input->description_uri = source.uri;
      step = phase::description;
      return begin_read(source.uri, maximum_bytes, true);
    }
    if (index == pending.size()) {
      step = phase::verify;
      return versions->begin_verification(verification, deferred_reads);
    }
    return GNEISS_SUCCESS;
  }
  gneiss_result read_description(std::size_t& budget, const std::function<bool()>& cancelled) {
    const auto loaded = read_step(budget, cancelled);
    if (loaded != GNEISS_SUCCESS || !read_complete) {
      return loaded;
    }
    input->description = std::move(bytes);
    reader.reset();
    if (current.source.type != asset_type::texture) {
      step = phase::decode;
      return GNEISS_SUCCESS;
    }
    texture_source source;
    const auto parsed = parse_texture(input->description, source, diagnostic);
    if (parsed != GNEISS_SUCCESS) {
      return parsed;
    }
    input->image_uri = source.uri;
    selected =
        profile.generation != 0U && std::string_view(source.uri).ends_with(".gneiss-texture");
    step = phase::payload;
    const auto limit = std::min(maximum_bytes - batch.bytes, std::size_t{64U} * 1024U * 1024U);
    return begin_read(source.uri, limit, !selected);
  }
  gneiss_result read_payload(std::size_t& budget, const std::function<bool()>& cancelled) {
    const auto loaded = read_step(budget, cancelled);
    if (loaded != GNEISS_SUCCESS || !read_complete) {
      return loaded;
    }
    if (selected) {
      input->source = std::move(reader);
    } else {
      input->image = std::move(bytes);
    }
    reader.reset();
    step = phase::decode;
    return GNEISS_SUCCESS;
  }
  gneiss_result decode() {
    auto decoded = GNEISS_SUCCESS;
    if (current.source.type == asset_type::texture) {
      virtual_file_system prepared;
      decoded = prepared.mount("asset://", input);
      if (decoded == GNEISS_SUCCESS) {
        decoded = prepare_texture(
            prepared, current.source.uri, current.texture, diagnostic,
            std::min(maximum_bytes, std::size_t{64U} * 1024U * 1024U), false,
            std::min(maximum_bytes - batch.bytes, std::size_t{64U} * 1024U * 1024U), profile);
      }
      current.bytes = current.texture.manifest.size() + current.texture.payload.size();
      for (const auto& level : current.texture.levels) {
        current.bytes += level.pixels.size();
      }
    } else {
      decoded = decode_non_texture(current.source, input->description, current, diagnostic,
                                   material_bytes, maximum_bytes, pending);
    }
    if (decoded != GNEISS_SUCCESS) {
      return decoded;
    }
    if (current.bytes > maximum_bytes - batch.bytes) {
      fail(diagnostic, GNEISS_ERROR_OUT_OF_MEMORY, current.source.uri,
           "资产准备预算不足：需要 " + std::to_string(current.bytes) + " 字节，可用 " +
               std::to_string(maximum_bytes - batch.bytes) + " 字节，批次上限 " +
               std::to_string(maximum_bytes) + " 字节");
      return GNEISS_ERROR_OUT_OF_MEMORY;
    }
    batch.bytes += current.bytes;
    batch.assets.push_back(std::move(current));
    input.reset();
    step = phase::select;
    return GNEISS_SUCCESS;
  }
  gneiss_result advance_once(std::size_t& budget, const std::function<bool()>& cancelled,
                             bool& complete) {
    switch (step) {
    case phase::select:
      return select();
    case phase::description:
      return read_description(budget, cancelled);
    case phase::payload:
      return read_payload(budget, cancelled);
    case phase::decode:
      return decode();
    case phase::verify: {
      const auto checked = verification->advance(budget, cancelled, complete);
      budget = 0U;
      if (checked != GNEISS_SUCCESS) {
        fail(diagnostic, checked, "", "准备期间源变化或请求取消");
      }
      return checked;
    }
    case phase::finished:
      complete = true;
      return result;
    }
    return GNEISS_ERROR_INTERNAL;
  }
  gneiss_result advance(std::size_t budget, const std::function<bool()>& cancelled,
                        bool& complete) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(8);
    // 小描述和状态转换共用一份预算，避免每个转换都等待下一宿主帧。
    // 时间边界仅在操作之间检查，单次解析/解码仍需单独测量。
    for (unsigned transitions = 0U; transitions < 64U; ++transitions) {
      if (cancelled && cancelled()) {
        return GNEISS_ERROR_INVALID_STATE;
      }
      const auto advanced = advance_once(budget, cancelled, complete);
      if (advanced != GNEISS_SUCCESS || complete || budget == 0U ||
          std::chrono::steady_clock::now() >= deadline) {
        return advanced;
      }
    }
    return GNEISS_SUCCESS;
  }
};
asset_preparation::asset_preparation(const virtual_file_system& files,
                                     std::span<const asset_request> requested, limits budget,
                                     texture_prepare_profile profile)
    : state_(std::make_unique<state>()) {
  auto& value = *state_;
  value.versions = std::make_shared<source_revision_file_system>(files);
  value.result = value.files.mount("asset://", value.versions);
  value.pending.assign(requested.begin(), requested.end());
  value.material_bytes = budget.material_bytes;
  value.maximum_assets = budget.maximum_assets;
  value.maximum_bytes = budget.maximum_bytes;
  value.deferred_reads = budget.deferred_reads;
  value.profile = profile;
  if (requested.empty() || requested.size() > budget.maximum_assets) {
    value.result = GNEISS_ERROR_INVALID_ARGUMENT;
  }
}
asset_preparation::~asset_preparation() = default;
gneiss_result asset_preparation::advance(std::size_t byte_budget,
                                         const std::function<bool()>& cancelled,
                                         prepared_batch& output, asset_diagnostic& diagnostic,
                                         bool& complete) noexcept {
  complete = false;
  auto& value = *state_;
  if (value.step == state::phase::finished) {
    complete = true;
    return value.result;
  }
  if (byte_budget == 0U) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    if (value.result == GNEISS_SUCCESS) {
      value.result = value.advance(byte_budget, cancelled, complete);
    }
    if (value.result == GNEISS_SUCCESS && complete) {
      std::ranges::stable_sort(value.batch.assets, {},
                               [](const auto& asset) { return asset_order(asset.source.type); });
      output = std::move(value.batch);
    }
    if (value.result != GNEISS_SUCCESS && value.diagnostic.message.empty()) {
      fail(value.diagnostic, value.result, value.current.source.uri, "资产读取、校验或解析失败");
    }
  } catch (const std::bad_alloc&) {
    value.result = GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    value.result = GNEISS_ERROR_INTERNAL;
  }
  if (value.result != GNEISS_SUCCESS || complete) {
    if (value.result != GNEISS_SUCCESS) {
      output = {};
    }
    value.diagnostic.result = value.result;
    diagnostic = std::move(value.diagnostic);
    value.batch = {};
    value.current = {};
    value.input.reset();
    value.reader.reset();
    value.slice = {};
    value.prefetched.clear();
    value.verification.reset();
    std::vector<std::byte>{}.swap(value.bytes);
    value.step = state::phase::finished;
    complete = true;
  }
  return value.result;
}
gneiss_result prepare_assets(const virtual_file_system& files,
                             std::span<const asset_request> requested, prepared_batch& output,
                             asset_diagnostic& diagnostic, const std::function<bool()>& cancelled,
                             std::size_t material_bytes, std::size_t maximum_assets,
                             std::size_t maximum_bytes, texture_prepare_profile profile) noexcept {
  output = {};
  diagnostic = {};
  try {
    asset_preparation preparation(files, requested,
                                  {
                                      .material_bytes = material_bytes,
                                      .maximum_assets = maximum_assets,
                                      .maximum_bytes = maximum_bytes,
                                      .deferred_reads = false,
                                  },
                                  profile);
    bool complete{};
    auto result = GNEISS_SUCCESS;
    while (result == GNEISS_SUCCESS && !complete) {
      result = preparation.advance(std::size_t{4U} * 1024U * 1024U, cancelled, output, diagnostic,
                                   complete);
    }
    return result;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result prepare_texture(const asset_internal::virtual_file_system& file_system,
                              std::string_view uri, asset_internal::prepared_texture_data& output,
                              asset_diagnostic& out_diagnostic, std::size_t input_limit,
                              bool verify_source, std::size_t output_limit,
                              texture_prepare_profile profile) noexcept {
  output = {};
  out_diagnostic = {};
  try {
    std::vector<std::byte> description_bytes;
    auto result = file_system.read_bounded(uri, input_limit, description_bytes);
    if (result != GNEISS_SUCCESS) {
      fail(out_diagnostic, result, "", "无法通过 VFS 读取 Texture 描述");
      return result;
    }
    texture_source source;
    result = parse_texture(description_bytes, source, out_diagnostic);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    std::vector<std::byte> image_bytes;
    std::shared_ptr<asset_internal::texture_container> container;
    std::shared_ptr<asset_internal::source_revision_file_system> revisions;
    const bool selected =
        profile.generation != 0U && std::string_view(source.uri).ends_with(".gneiss-texture");
    if (selected) {
      container = std::make_shared<asset_internal::texture_container>();
      std::unique_ptr<asset_internal::read_source> reader;
      if (verify_source) {
        revisions = std::make_shared<asset_internal::source_revision_file_system>(file_system);
        asset_internal::virtual_file_system checked;
        result = checked.mount("asset://", revisions);
        if (result == GNEISS_SUCCESS) {
          result = checked.open_read(source.uri, reader);
        }
      } else {
        result = file_system.open_read(source.uri, reader);
      }
      std::string message;
      if (result == GNEISS_SUCCESS) {
        result = container->open(std::move(reader), std::min(input_limit, output_limit), message);
      }
      if (result != GNEISS_SUCCESS) {
        fail(out_diagnostic, result, "/source",
             message.empty() ? "无法按范围读取运行纹理" : message);
        return result;
      }
    } else {
      result = file_system.read_bounded(source.uri,
                                        input_limit == std::numeric_limits<std::size_t>::max()
                                            ? input_limit
                                            : std::min(input_limit, output_limit),
                                        image_bytes);
      if (result != GNEISS_SUCCESS) {
        fail(out_diagnostic, result, "/source", "无法通过 VFS 读取纹理数据");
        return result;
      }
    }
    if (std::string_view(source.uri).ends_with(".gneiss-texture")) {
#if defined(GNEISS_HAS_GRANIT_PLATFORM)
      asset_internal::texture_binary_view binary;
      std::string decode_message;
      if (selected) {
        binary.manifest = container->manifest();
      } else if (asset_internal::decode_texture_binary(image_bytes, binary, decode_message) !=
                 asset_internal::texture_binary_result::success) {
        fail(out_diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "/source",
             decode_message.empty() ? "运行纹理封装检查失败" : decode_message);
        return GNEISS_ERROR_INVALID_ARGUMENT;
      }
      granit::texture_asset_info info;
      if (granit::inspect_texture_asset(binary.manifest, info) != granit::result::success ||
          info.dimension != granit::texture_dimension::two_dimensional || info.depth != 1U ||
          info.array_layers != 1U) {
        fail(out_diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "/source",
             "Granit Texture Asset Manifest 无效或不是二维单层纹理");
        return GNEISS_ERROR_INVALID_ARGUMENT;
      }
      const auto srgb = source.color_space == GNEISS_TEXTURE_COLOR_SPACE_SRGB;
      const auto matches_color_space = [srgb](const auto& variant) {
        return srgb ? variant.format == granit::texture_format::bc7_rgba_srgb ||
                          variant.format == granit::texture_format::rgba8_srgb
                    : variant.format == granit::texture_format::bc7_rgba_unorm ||
                          variant.format == granit::texture_format::rgba8_unorm;
      };
      const auto payload_size = selected ? container->payload_size() : binary.payload.size();
      const auto payload_in_bounds = [payload_size](const auto& variant) {
        return variant.payload_offset <= payload_size &&
               variant.payload_size <= payload_size - variant.payload_offset;
      };
      const auto fallback_format =
          srgb ? granit::texture_format::rgba8_srgb : granit::texture_format::rgba8_unorm;
      const auto has_fallback =
          std::ranges::any_of(info.variants, [fallback_format](const auto& variant) {
            return variant.format == fallback_format;
          });
      if (!std::ranges::all_of(info.variants, matches_color_space) ||
          !std::ranges::all_of(info.variants, payload_in_bounds) || !has_fallback) {
        fail(out_diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "/source",
             "运行纹理变体颜色空间、负载边界或 RGBA8 回退无效");
        return GNEISS_ERROR_INVALID_ARGUMENT;
      }
      output = {
          .width = info.width,
          .height = info.height,
          .format = GNEISS_TEXTURE_FORMAT_RGBA8_UNORM,
          .color_space = source.color_space,
          .levels = {},
          .manifest = std::vector<std::byte>(binary.manifest.begin(), binary.manifest.end()),
          .payload = std::vector<std::byte>(binary.payload.begin(), binary.payload.end()),
      };
      if (selected) {
        constexpr std::array formats{
            granit::texture_format::rgba8_unorm, granit::texture_format::rgba8_srgb,
            granit::texture_format::bc7_rgba_unorm, granit::texture_format::bc7_rgba_srgb};
        constexpr auto usage = static_cast<std::uint32_t>(
            granit::texture_usage::sampled | granit::texture_usage::transfer_destination);
        for (std::size_t index = 0U; index < info.variants.size(); ++index) {
          const auto& variant = info.variants[index];
          const auto format = std::ranges::find(formats, variant.format);
          if (format == formats.end() ||
              !profile
                   .sampled_transfer_formats[static_cast<std::size_t>(format - formats.begin())] ||
              (static_cast<std::uint32_t>(variant.usage) & usage) != usage) {
            continue;
          }
          if (output.manifest.size() > output_limit ||
              variant.payload_size > output_limit - output.manifest.size() ||
              variant.payload_size > input_limit) {
            const auto manifest_bytes = output.manifest.size();
            output = {};
            fail(out_diagnostic, GNEISS_ERROR_OUT_OF_MEMORY, "/source",
                 "纹理准备预算不足：所选负载需要 " + std::to_string(variant.payload_size) +
                     " 字节，Manifest 需要 " + std::to_string(manifest_bytes) +
                     " 字节，负载读取上限 " + std::to_string(input_limit) + " 字节，合计可用 " +
                     std::to_string(output_limit) + " 字节");
            return GNEISS_ERROR_OUT_OF_MEMORY;
          }
          output.payload.resize(static_cast<std::size_t>(variant.payload_size));
          result = container->read_payload(variant.payload_offset, output.payload);
          if (result == GNEISS_SUCCESS && core::sha256(output.payload) != variant.payload_digest) {
            result = GNEISS_ERROR_INVALID_ARGUMENT;
          }
          if (result != GNEISS_SUCCESS) {
            output = {};
            fail(out_diagnostic, result, "/source", "选中纹理变体读取或摘要校验失败");
            return result;
          }
          output.payload_source = std::make_shared<asset_internal::texture_payload_source>(
              container, variant.payload_offset, variant.payload_size, variant.payload_digest);
          output.profile = profile;
          output.selected_variant = static_cast<std::uint32_t>(index);
          break;
        }
        if (output.profile.generation == 0U) {
          output = {};
          fail(out_diagnostic, GNEISS_ERROR_UNSUPPORTED, "/source", "设备不支持运行纹理的任何变体");
          return GNEISS_ERROR_UNSUPPORTED;
        }
      }
#else
      fail(out_diagnostic, GNEISS_ERROR_UNSUPPORTED, "/source",
           "当前构建未启用 Granit，无法加载运行纹理封装");
      return GNEISS_ERROR_UNSUPPORTED;
#endif
    } else if (std::string_view(source.uri).ends_with(".ktx2")) {
      asset_internal::texture_ktx2 texture;
      std::string decode_message;
      const auto decoded =
          asset_internal::decode_texture_ktx2(image_bytes, texture, decode_message);
      if (decoded != asset_internal::texture_ktx2_result::success) {
        fail(out_diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "/source",
             decode_message.empty() ? "KTX2 解码失败" : decode_message);
        return GNEISS_ERROR_INVALID_ARGUMENT;
      }
      const auto expected_transfer = source.color_space == GNEISS_TEXTURE_COLOR_SPACE_SRGB
                                         ? asset_internal::texture_transfer::srgb
                                         : asset_internal::texture_transfer::linear;
      if (texture.transfer != expected_transfer) {
        fail(out_diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "/color_space",
             "Texture 描述与 KTX2 传递函数不一致");
        return GNEISS_ERROR_INVALID_ARGUMENT;
      }
      auto levels = std::move(texture.levels);
      const auto width = levels.front().width;
      const auto height = levels.front().height;
      output = {.width = width,
                .height = height,
                .format = GNEISS_TEXTURE_FORMAT_RGBA8_UNORM,
                .color_space = source.color_space,
                .levels = std::move(levels),
                .manifest = {},
                .payload = {}};
    } else {
      asset_internal::decoded_png image;
      std::string decode_message;
      result = asset_internal::decode_png(image_bytes, image, decode_message, output_limit);
      if (result != GNEISS_SUCCESS) {
        fail(out_diagnostic, result, "/source",
             decode_message.empty() ? "PNG 解码失败" : decode_message);
        return result;
      }
      output = {.width = image.width,
                .height = image.height,
                .format = GNEISS_TEXTURE_FORMAT_RGBA8_UNORM,
                .color_space = source.color_space,
                .levels = {},
                .manifest = {},
                .payload = {}};
      output.levels.push_back(
          {.width = image.width, .height = image.height, .pixels = std::move(image.pixels)});
    }
    if (verify_source) {
      std::vector<std::byte> checked;
      if (file_system.read_bounded(uri, input_limit, checked) != GNEISS_SUCCESS ||
          checked != description_bytes ||
          (selected
               ? revisions->verify({}) != GNEISS_SUCCESS
               : file_system.read_bounded(source.uri, input_limit, checked) != GNEISS_SUCCESS ||
                     checked != image_bytes)) {
        output = {};
        fail(out_diagnostic, GNEISS_ERROR_INVALID_STATE, "/source", "准备期间纹理源已变化");
        return GNEISS_ERROR_INVALID_STATE;
      }
    }
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    output = {};
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    output = {};
    return GNEISS_ERROR_INTERNAL;
  }
}

} // namespace gneiss::asset_internal
