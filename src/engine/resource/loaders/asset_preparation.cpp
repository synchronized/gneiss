// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/resource/loaders/asset_preparation.hpp"
#include "engine/core/result.hpp"

#include "engine/resource/loaders/asset_parsing.hpp"

#include "engine/core/diagnostics/profiling.hpp"
#include "engine/resource/loaders/mesh_binary.hpp"
#include "engine/resource/loaders/png_decoder.hpp"
#include "engine/resource/loaders/texture_binary.hpp"
#include "engine/resource/loaders/texture_ktx2.hpp"

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

namespace gneiss::engine::detail {

void fail(asset_diagnostic& diagnostic, gneiss::engine::result result, std::string_view path,
          std::string_view message, std::size_t offset) noexcept {
  diagnostic.result = result;
  diagnostic.byte_offset = offset;
  try {
    diagnostic.path = path;
    diagnostic.message = message;
  } catch (...) {
    diagnostic.result = gneiss::engine::result::out_of_memory;
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
      fail(diagnostic, gneiss::engine::result::invalid_argument, "/" + std::string(name),
           "未知字段");
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
    if (diagnostic.result == gneiss::engine::result::success) {
      fail(diagnostic, gneiss::engine::result::invalid_argument, "", "资产根必须是对象");
    }
    return false;
  }
  yyjson_val* format = yyjson_obj_get(root, "format");
  if (!yyjson_is_str(format) || json_string(format) != expected_format) {
    fail(diagnostic, gneiss::engine::result::invalid_argument, "/format", "资产格式标识错误");
    return false;
  }
  yyjson_val* version = yyjson_obj_get(root, "version");
  if (!yyjson_is_uint(version) || yyjson_get_uint(version) == 0U ||
      yyjson_get_uint(version) > maximum_version) {
    fail(diagnostic,
         yyjson_is_uint(version) && yyjson_get_uint(version) > 1U
             ? gneiss::engine::result::unsupported
             : gneiss::engine::result::invalid_argument,
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
    fail(diagnostic, gneiss::engine::result::invalid_argument, "", "资产文档为空");
    return {nullptr, &yyjson_doc_free};
  }
  yyjson_read_err error{};
  document_ptr document(
      yyjson_read_opts(reinterpret_cast<char*>(const_cast<std::byte*>(bytes.data())), bytes.size(),
                       YYJSON_READ_NOFLAG, nullptr, &error),
      &yyjson_doc_free);
  if (!document) {
    fail(diagnostic, gneiss::engine::result::invalid_argument, "",
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

[[nodiscard]] gneiss::engine::result parse_pbr_extensions(yyjson_val* root,
                                                          material_description& out_source,
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
      fail(diagnostic, gneiss::engine::result::invalid_argument, std::string{"/"} + names[slot],
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
    fail(diagnostic, gneiss::engine::result::invalid_argument, "/normal_scale",
         "法线缩放必须有限，AO 强度必须位于 0..1");
    return diagnostic.result;
  }
  if (auto* emissive = yyjson_obj_get(root, "emissive")) {
    if (!yyjson_is_arr(emissive) || yyjson_arr_size(emissive) != 3U) {
      fail(diagnostic, gneiss::engine::result::invalid_argument, "/emissive",
           "自发光必须包含三个分量");
      return diagnostic.result;
    }
    for (std::size_t i = 0; i < 3U; ++i) {
      if (!read_float(yyjson_arr_get(emissive, i), out_source.parameters.emissive[i]) ||
          out_source.parameters.emissive[i] < 0.0F || out_source.parameters.emissive[i] > 1.0F) {
        fail(diagnostic, gneiss::engine::result::invalid_argument, "/emissive",
             "自发光分量必须位于 0..1");
        return diagnostic.result;
      }
    }
  }
  return gneiss::engine::result::success;
}

[[nodiscard]] bool parse_material_sampling(yyjson_val* root,
                                           gneiss::engine::material_parameters& state) {
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
      std::array<std::uint32_t, 6> fields{};
      for (std::size_t field = 0; field < names.size(); ++field) {
        auto* value = yyjson_obj_get(item, names[field]);
        if (!yyjson_is_uint(value) || yyjson_get_uint(value) > limits[field]) {
          return false;
        }
        fields[field] = static_cast<std::uint32_t>(yyjson_get_uint(value));
      }
      sample = {fields[0],
                static_cast<gneiss::engine::texture_filter>(fields[1]),
                static_cast<gneiss::engine::texture_filter>(fields[2]),
                static_cast<gneiss::engine::texture_mip_filter>(fields[3]),
                static_cast<gneiss::engine::texture_address>(fields[4]),
                static_cast<gneiss::engine::texture_address>(fields[5])};
    }
  }
  return true;
}

[[nodiscard]] bool parse_material_state(yyjson_val* root,
                                        gneiss::engine::material_parameters& state) {
  if (auto* mode = yyjson_obj_get(root, "alpha_mode")) {
    if (!yyjson_is_str(mode)) {
      return false;
    }
    const auto name = json_string(mode);
    if (name == "OPAQUE") {
      state.alpha_mode = gneiss::engine::material_alpha_mode::opaque;
    } else if (name == "MASK") {
      state.alpha_mode = gneiss::engine::material_alpha_mode::mask;
    } else if (name == "BLEND") {
      state.alpha_mode = gneiss::engine::material_alpha_mode::blend;
    } else {
      return false;
    }
  }
  if (auto* sided = yyjson_obj_get(root, "double_sided")) {
    if (!yyjson_is_bool(sided)) {
      return false;
    }
    state.double_sided = yyjson_get_bool(sided);
  }
  if (auto* cutoff = yyjson_obj_get(root, "alpha_cutoff");
      cutoff && (!read_float(cutoff, state.alpha_cutoff) || state.alpha_cutoff < 0.0F)) {
    return false;
  }

  return parse_material_sampling(root, state);
}

[[nodiscard]] gneiss::engine::result parse_material(const std::vector<std::byte>& bytes,
                                                    material_description& out_source,
                                                    asset_diagnostic& diagnostic) {
  auto document = parse_document(bytes, diagnostic);
  if (!document) {
    return diagnostic.result;
  }
  yyjson_val* root = yyjson_doc_get_root(document.get());
  constexpr std::array fields{
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
      std::string_view{"alpha_mode"},
      std::string_view{"double_sided"},
      std::string_view{"alpha_cutoff"},
      std::string_view{"sampling"},
  };
  std::uint64_t version{};
  if (!validate_header(root, "gneiss.material", fields, 5U, version, diagnostic))
    return diagnostic.result;
  if (version != 5U) {
    fail(diagnostic, result::unsupported, "/version", "材质仅支持版本 5");
    return diagnostic.result;
  }
  yyjson_val* color = yyjson_obj_get(root, "color");
  if (!yyjson_is_arr(color) || yyjson_arr_size(color) != out_source.parameters.color.size()) {
    fail(diagnostic, gneiss::engine::result::invalid_argument, "/color", "颜色必须包含四个分量");
    return diagnostic.result;
  }
  for (std::size_t index = 0; index < out_source.parameters.color.size(); ++index) {
    if (!read_float(yyjson_arr_get(color, index), out_source.parameters.color[index]) ||
        out_source.parameters.color[index] < 0.0F || out_source.parameters.color[index] > 1.0F) {
      fail(diagnostic, gneiss::engine::result::invalid_argument, "/color/" + std::to_string(index),
           "颜色分量必须位于 0..1");
      return diagnostic.result;
    }
  }
  {
    yyjson_val* texture = yyjson_obj_get(root, "base_color_texture");
    if (yyjson_is_null(texture)) {
      out_source.texture_uris[0].clear();
    } else if (!yyjson_is_str(texture) || yyjson_get_len(texture) == 0U) {
      fail(diagnostic, gneiss::engine::result::invalid_argument, "/base_color_texture",
           "base-color Texture 必须是非空 URI");
      return diagnostic.result;
    } else {
      out_source.texture_uris[0].assign(json_string(texture));
    }
  }
  if (!read_float(yyjson_obj_get(root, "metallic"), out_source.parameters.metallic) ||
      out_source.parameters.metallic < 0.0F || out_source.parameters.metallic > 1.0F ||
      !read_float(yyjson_obj_get(root, "roughness"), out_source.parameters.roughness) ||
      out_source.parameters.roughness < 0.0F || out_source.parameters.roughness > 1.0F) {
    fail(diagnostic, gneiss::engine::result::invalid_argument, "/metallic",
         "metallic 与 roughness 必须位于 0..1");
    return diagnostic.result;
  }

  if (parse_pbr_extensions(root, out_source, diagnostic) != gneiss::engine::result::success) {
    return diagnostic.result;
  }
  if (!parse_material_state(root, out_source.parameters)) {
    fail(diagnostic, gneiss::engine::result::invalid_argument, "/sampling",
         "材质状态或逐槽采样设置无效");
    return diagnostic.result;
  }
  return gneiss::engine::result::success;
}

[[nodiscard]] gneiss::engine::result parse_texture(const std::vector<std::byte>& bytes,
                                                   texture_description& out_source,
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
    fail(diagnostic, gneiss::engine::result::invalid_argument, "/source",
         "Texture source 必须是非空 URI");
    return diagnostic.result;
  }
  yyjson_val* color_space = yyjson_obj_get(root, "color_space");
  if (!yyjson_is_str(color_space)) {
    fail(diagnostic, gneiss::engine::result::invalid_argument, "/color_space",
         "Texture 颜色空间必须是字符串");
    return diagnostic.result;
  }
  const auto color_space_name = json_string(color_space);
  if (color_space_name == "srgb") {
    out_source.color_space = gneiss::engine::texture_color_space::srgb;
  } else if (color_space_name == "linear") {
    out_source.color_space = gneiss::engine::texture_color_space::linear;
  } else {
    fail(diagnostic, gneiss::engine::result::invalid_argument, "/color_space",
         "只支持 srgb 或 linear");
    return diagnostic.result;
  }
  if (version == 2U) {
    auto* const usage = yyjson_obj_get(root, "usage");
    if (!yyjson_is_str(usage) || json_string(usage) != "normal" ||
        out_source.color_space != gneiss::engine::texture_color_space::linear) {
      fail(diagnostic, gneiss::engine::result::invalid_argument, "/usage",
           "Texture v2 当前只支持 linear 法线用途");
      return diagnostic.result;
    }
  }
  out_source.uri.assign(json_string(source));
  return gneiss::engine::result::success;
}

} // namespace gneiss::engine::detail

namespace gneiss::engine {
using namespace gneiss::engine::detail;

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
gneiss::engine::result decode_non_texture(const asset_preparation_source& source,
                                          const std::vector<std::byte>& bytes,
                                          prepared_asset& asset, asset_diagnostic& diagnostic,
                                          std::size_t material_bytes, std::size_t maximum_bytes,
                                          std::vector<asset_preparation_source>& pending,
                                          bool include_dependencies) {
  auto result = gneiss::engine::result::success;
  if (source.type == asset_type::mesh) {
    result =
        gneiss::engine::is_mesh_binary(bytes)
            ? parse_binary_mesh(bytes, asset.mesh.vertices, asset.mesh.normals, asset.mesh.indices,
                                asset.mesh.tangents, asset.mesh.uv1, asset.mesh.colors, diagnostic)
            : parse_mesh(bytes, asset.mesh.vertices, asset.mesh.normals, diagnostic);
    // 此校验与 RID 创建的顶点/法线/索引契约一致，在后台完成。
    const auto& mesh = asset.mesh;
    if (result == gneiss::engine::result::success && invalid_prepared_mesh(mesh)) {
      result = gneiss::engine::result::invalid_argument;
    }
    asset.bytes = mesh.data_bytes();
  } else if (source.type == asset_type::material) {
    if (!asset.description)
      return gneiss::engine::result::unsupported;
    asset.material = asset.description->parameters;
    asset.texture_uris = asset.description->texture_uris;
    // 由调用方传入发布预算，Asset 不依赖资源对象布局。
    if (material_bytes == 0U) {
      fail(diagnostic, gneiss::engine::result::invalid_argument, source.uri,
           "材质发布预算不得为零");
      return gneiss::engine::result::invalid_argument;
    }
    if (material_bytes > maximum_bytes) {
      fail(diagnostic, gneiss::engine::result::out_of_memory, source.uri,
           "材质发布预算超过批次上限");
      return gneiss::engine::result::out_of_memory;
    }
    asset.bytes = material_bytes;
    for (const auto& uri : asset.texture_uris) {
      if (include_dependencies && result == gneiss::engine::result::success && !uri.empty()) {
        pending.push_back({.uri = uri, .type = asset_type::texture});
      }
      if (uri.size() > maximum_bytes - asset.bytes) {
        fail(diagnostic, gneiss::engine::result::out_of_memory, source.uri,
             "材质依赖超出批次字节上限");
        return gneiss::engine::result::out_of_memory;
      }
      asset.bytes += uri.size();
    }
  } else {
    result = gneiss::engine::result::invalid_argument;
  }
  return result;
}
} // namespace

namespace {
result decode_texture_payload(const texture_description& source,
                              const std::vector<std::byte>& image_bytes,
                              prepared_texture_data& output, asset_diagnostic& out_diagnostic,
                              [[maybe_unused]] std::size_t input_limit, std::size_t output_limit,
                              texture_prepare_profile profile) noexcept {
  output = {};
  out_diagnostic = {};
  try {
    auto result = gneiss::engine::result::success;
    [[maybe_unused]] const bool selected =
        profile.generation != 0U && std::string_view(source.uri).ends_with(".gneiss-texture");
    if (std::string_view(source.uri).ends_with(".gneiss-texture")) {
#if defined(GNEISS_HAS_GRANIT_PLATFORM)
      gneiss::engine::texture_binary_view binary;
      std::string decode_message;
      if (gneiss::engine::decode_texture_binary(image_bytes, binary, decode_message) !=
          gneiss::engine::texture_binary_result::success) {
        fail(out_diagnostic, gneiss::engine::result::invalid_argument, "/source",
             decode_message.empty() ? "运行纹理封装检查失败" : decode_message);
        return gneiss::engine::result::invalid_argument;
      }
      granit::texture_asset_info info;
      if (granit::inspect_texture_asset(binary.manifest, info) != granit::result::success ||
          info.dimension != granit::texture_dimension::two_dimensional || info.depth != 1U ||
          info.array_layers != 1U) {
        fail(out_diagnostic, gneiss::engine::result::invalid_argument, "/source",
             "Granit Texture Asset Manifest 无效或不是二维单层纹理");
        return gneiss::engine::result::invalid_argument;
      }
      const auto srgb = source.color_space == gneiss::engine::texture_color_space::srgb;
      const auto matches_color_space = [srgb](const auto& variant) {
        return srgb ? variant.format == granit::texture_format::bc7_rgba_srgb ||
                          variant.format == granit::texture_format::rgba8_srgb
                    : variant.format == granit::texture_format::bc7_rgba_unorm ||
                          variant.format == granit::texture_format::rgba8_unorm;
      };
      const auto payload_size = binary.payload.size();
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
        fail(out_diagnostic, gneiss::engine::result::invalid_argument, "/source",
             "运行纹理变体颜色空间、负载边界或 RGBA8 回退无效");
        return gneiss::engine::result::invalid_argument;
      }
      output = {
          .width = info.width,
          .height = info.height,
          .format = gneiss::engine::texture_format::rgba8_unorm,
          .color_space = source.color_space,
          .levels = {},
          .manifest = std::vector<std::byte>(binary.manifest.begin(), binary.manifest.end()),
          .payload = selected
                         ? std::vector<std::byte>{}
                         : std::vector<std::byte>(binary.payload.begin(), binary.payload.end()),
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
            fail(out_diagnostic, gneiss::engine::result::out_of_memory, "/source",
                 "纹理准备预算不足：所选负载需要 " + std::to_string(variant.payload_size) +
                     " 字节，Manifest 需要 " + std::to_string(manifest_bytes) +
                     " 字节，负载读取上限 " + std::to_string(input_limit) + " 字节，合计可用 " +
                     std::to_string(output_limit) + " 字节");
            return gneiss::engine::result::out_of_memory;
          }
          output.payload.resize(static_cast<std::size_t>(variant.payload_size));
          const auto selected_bytes = binary.payload.subspan(
              static_cast<std::size_t>(variant.payload_offset), output.payload.size());
          std::copy(selected_bytes.begin(), selected_bytes.end(), output.payload.begin());
          if (result == gneiss::engine::result::success &&
              gneiss::engine::sha256(output.payload) != variant.payload_digest) {
            result = gneiss::engine::result::invalid_argument;
          }
          if (result != gneiss::engine::result::success) {
            output = {};
            fail(out_diagnostic, result, "/source", "选中纹理变体读取或摘要校验失败");
            return result;
          }
          output.profile = profile;
          output.selected_variant = static_cast<std::uint32_t>(index);
          break;
        }
        if (output.profile.generation == 0U) {
          output = {};
          fail(out_diagnostic, gneiss::engine::result::unsupported, "/source",
               "设备不支持运行纹理的任何变体");
          return gneiss::engine::result::unsupported;
        }
      }
#else
      fail(out_diagnostic, gneiss::engine::result::unsupported, "/source",
           "当前构建未启用 Granit，无法加载运行纹理封装");
      return gneiss::engine::result::unsupported;
#endif
    } else if (std::string_view(source.uri).ends_with(".ktx2")) {
      gneiss::engine::texture_ktx2 texture;
      std::string decode_message;
      const auto decoded =
          gneiss::engine::decode_texture_ktx2(image_bytes, texture, decode_message);
      if (decoded != gneiss::engine::texture_ktx2_result::success) {
        fail(out_diagnostic, gneiss::engine::result::invalid_argument, "/source",
             decode_message.empty() ? "KTX2 解码失败" : decode_message);
        return gneiss::engine::result::invalid_argument;
      }
      const auto expected_transfer = source.color_space == gneiss::engine::texture_color_space::srgb
                                         ? gneiss::engine::texture_transfer::srgb
                                         : gneiss::engine::texture_transfer::linear;
      if (texture.transfer != expected_transfer) {
        fail(out_diagnostic, gneiss::engine::result::invalid_argument, "/color_space",
             "Texture 描述与 KTX2 传递函数不一致");
        return gneiss::engine::result::invalid_argument;
      }
      auto levels = std::move(texture.levels);
      const auto width = levels.front().width;
      const auto height = levels.front().height;
      output = {.width = width,
                .height = height,
                .format = gneiss::engine::texture_format::rgba8_unorm,
                .color_space = source.color_space,
                .levels = std::move(levels),
                .manifest = {},
                .payload = {}};
    } else {
      gneiss::engine::decoded_png image;
      std::string decode_message;
      result = gneiss::engine::decode_png(image_bytes, image, decode_message, output_limit);
      if (result != gneiss::engine::result::success) {
        fail(out_diagnostic, result, "/source",
             decode_message.empty() ? "PNG 解码失败" : decode_message);
        return result;
      }
      output = {.width = image.width,
                .height = image.height,
                .format = gneiss::engine::texture_format::rgba8_unorm,
                .color_space = source.color_space,
                .levels = {},
                .manifest = {},
                .payload = {}};
      output.levels.push_back(
          {.width = image.width, .height = image.height, .pixels = std::move(image.pixels)});
    }

    return gneiss::engine::result::success;
  } catch (const std::bad_alloc&) {
    output = {};
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    output = {};
    return gneiss::engine::result::internal;
  }
}

} // namespace

result decode_loaded_asset(const asset_preparation_source& source,
                           std::vector<std::byte> description, std::string payload_uri,
                           std::vector<std::byte> payload, prepared_asset& output,
                           asset_diagnostic& diagnostic, std::size_t maximum_bytes,
                           texture_prepare_profile profile,
                           asset_ref<const material_description> material,
                           std::size_t material_bytes) {
  prepared_asset decoded;
  decoded.source = source;
  decoded.description = std::move(material);
  result status;
  if (source.type == asset_type::texture) {
    if (description.size() > maximum_bytes || payload.size() > maximum_bytes)
      return result::invalid_argument;
    texture_description texture;
    status = parse_texture(description, texture, diagnostic);
    if (status.failed())
      return status;
    if (texture.uri != payload_uri) {
      fail(diagnostic, result::invalid_argument, "/source", "纹理负载 URI 与描述不匹配");
      return result::invalid_argument;
    }
    status = decode_texture_payload(texture, payload, decoded.texture, diagnostic, maximum_bytes,
                                    maximum_bytes, profile);
    decoded.bytes = decoded.texture.manifest.size() + decoded.texture.payload.size();
    for (const auto& level : decoded.texture.levels)
      decoded.bytes += level.pixels.size();
  } else {
    std::vector<asset_preparation_source> unused;
    status = decode_non_texture(source, description, decoded, diagnostic, material_bytes,
                                maximum_bytes, unused, false);
  }
  if (status.failed())
    return status;
  if (decoded.bytes > maximum_bytes)
    return result::out_of_memory;
  output = std::move(decoded);
  return result::success;
}

} // namespace gneiss::engine
