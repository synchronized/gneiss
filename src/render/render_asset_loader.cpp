// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "render/render_asset_loader.h"

#include "asset/mesh_binary.h"
#include "asset/texture_binary.h"
#include "asset/texture_ktx2.h"
#include "asset/virtual_file_system.h"
#include "render/png_decoder.h"
#include "render/render_resource_service.h"

#include <yyjson.h>

#if defined(GNEISS_HAS_GRANIT_PLATFORM)
#include <granit/renderer/texture_asset.hpp>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <new>
#include <span>
#include <vector>

namespace {

using gneiss::render_internal::asset_diagnostic;

constexpr std::uint32_t mesh_type = 1;
constexpr std::uint32_t material_type = 2;
constexpr std::uint32_t texture_type = 3;

struct mesh_asset final {
  mesh_asset(gneiss::render_internal::render_resource_service& owner, gneiss_mesh value) noexcept
      : resources(&owner), rid(value) {}
  mesh_asset(const mesh_asset&) = delete;
  mesh_asset& operator=(const mesh_asset&) = delete;
  gneiss::render_internal::render_resource_service* resources;
  gneiss_mesh rid;
  ~mesh_asset() {
    if (resources != nullptr && rid != GNEISS_NULL_MESH) {
      (void)resources->destroy_mesh(rid);
    }
  }
};

struct material_asset final {
  material_asset(gneiss::render_internal::render_resource_service& owner, gneiss_material value,
                 std::array<std::shared_ptr<const gneiss::asset_internal::resource_cache::entry>, 5>
                     texture_dependencies = {}) noexcept
      : resources(&owner), rid(value), textures(std::move(texture_dependencies)) {}
  material_asset(const material_asset&) = delete;
  material_asset& operator=(const material_asset&) = delete;
  gneiss::render_internal::render_resource_service* resources;
  gneiss_material rid;
  std::array<std::shared_ptr<const gneiss::asset_internal::resource_cache::entry>, 5> textures;
  ~material_asset() {
    if (resources != nullptr && rid != GNEISS_NULL_MATERIAL) {
      (void)resources->destroy_material(rid);
    }
  }
};

struct texture_asset final {
  texture_asset(gneiss::render_internal::render_resource_service& owner,
                gneiss_texture value) noexcept
      : resources(&owner), rid(value) {}
  texture_asset(const texture_asset&) = delete;
  texture_asset& operator=(const texture_asset&) = delete;
  gneiss::render_internal::render_resource_service* resources;
  gneiss_texture rid;
  ~texture_asset() {
    if (resources != nullptr && rid != GNEISS_NULL_TEXTURE) {
      (void)resources->destroy_texture(rid);
    }
  }
};

struct texture_source final {
  std::string uri;
  std::uint32_t color_space{};
};

struct material_source final {
  std::array<float, 4> color{};
  std::array<std::string, 5> texture_uris;
  float metallic{};
  float roughness{1.0F};
  float normal_scale{1.0F};
  float occlusion_strength{1.0F};
  std::array<float, 3> emissive{};
};

void fail(asset_diagnostic& diagnostic, gneiss_result result, std::string_view path,
          std::string_view message, std::size_t offset = 0) noexcept {
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

[[nodiscard]] gneiss_result parse_binary_mesh(const std::vector<std::byte>& bytes,
                                              std::vector<gneiss_mesh_vertex>& out_vertices,
                                              std::vector<gneiss_mesh_normal>& out_normals,
                                              std::vector<std::uint32_t>& out_indices,
                                              std::vector<gneiss_mesh_tangent>& out_tangents,
                                              asset_diagnostic& diagnostic) {
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
    out_tangents.push_back({tangent[0], tangent[1], tangent[2], tangent[3]});
  }
  out_indices = std::move(data.indices);
  return GNEISS_SUCCESS;
}

[[nodiscard]] gneiss_result parse_pbr_extensions(yyjson_val* root, material_source& out_source,
                                                 asset_diagnostic& diagnostic) {
  constexpr std::array names{"base_color_texture", "metallic_roughness_texture", "normal_texture",
                             "occlusion_texture", "emissive_texture"};
  for (std::size_t slot = 1; slot < names.size(); ++slot) {
    auto* texture = yyjson_obj_get(root, names[slot]);
    if (texture == nullptr || yyjson_is_null(texture))
      continue;
    if (!yyjson_is_str(texture) || yyjson_get_len(texture) == 0U) {
      fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, std::string{"/"} + names[slot],
           "Texture 必须是非空 URI 或 null");
      return diagnostic.result;
    }
    out_source.texture_uris[slot].assign(json_string(texture));
  }
  auto* normal_scale = yyjson_obj_get(root, "normal_scale");
  auto* strength = yyjson_obj_get(root, "occlusion_strength");
  if ((normal_scale != nullptr && !read_float(normal_scale, out_source.normal_scale)) ||
      (strength != nullptr &&
       (!read_float(strength, out_source.occlusion_strength) ||
        out_source.occlusion_strength < 0.0F || out_source.occlusion_strength > 1.0F))) {
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
      if (!read_float(yyjson_arr_get(emissive, i), out_source.emissive[i]) ||
          out_source.emissive[i] < 0.0F || out_source.emissive[i] > 1.0F) {
        fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "/emissive", "自发光分量必须位于 0..1");
        return diagnostic.result;
      }
    }
  }
  return GNEISS_SUCCESS;
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
  constexpr std::array v4_fields{std::string_view{"format"},
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
                                 std::string_view{"emissive"}};
  std::uint64_t version = 0;
  auto fields = std::span<const std::string_view>{v1_fields};
  if (requested_version == 2U)
    fields = v2_fields;
  else if (requested_version == 3U)
    fields = v3_fields;
  else if (requested_version == 4U)
    fields = v4_fields;
  if (!validate_header(root, "gneiss.material", fields, 4U, version, diagnostic)) {
    return diagnostic.result;
  }
  yyjson_val* color = yyjson_obj_get(root, "color");
  if (!yyjson_is_arr(color) || yyjson_arr_size(color) != out_source.color.size()) {
    fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "/color", "颜色必须包含四个分量");
    return diagnostic.result;
  }
  for (std::size_t index = 0; index < out_source.color.size(); ++index) {
    if (!read_float(yyjson_arr_get(color, index), out_source.color[index]) ||
        out_source.color[index] < 0.0F || out_source.color[index] > 1.0F) {
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
  if (version >= 3U) {
    if (!read_float(yyjson_obj_get(root, "metallic"), out_source.metallic) ||
        out_source.metallic < 0.0F || out_source.metallic > 1.0F ||
        !read_float(yyjson_obj_get(root, "roughness"), out_source.roughness) ||
        out_source.roughness < 0.0F || out_source.roughness > 1.0F) {
      fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "/metallic",
           "metallic 与 roughness 必须位于 0..1");
      return diagnostic.result;
    }
  }
  if (version == 4U) {
    return parse_pbr_extensions(root, out_source, diagnostic);
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
  constexpr std::array v1_fields{std::string_view{"format"}, std::string_view{"version"},
                                 std::string_view{"source"}, std::string_view{"color_space"}};
  constexpr std::array v2_fields{std::string_view{"format"}, std::string_view{"version"},
                                 std::string_view{"source"}, std::string_view{"color_space"},
                                 std::string_view{"usage"}};
  const auto requested = yyjson_obj_get(root, "version");
  const auto fields = yyjson_is_uint(requested) && yyjson_get_uint(requested) == 2U
                          ? std::span<const std::string_view>{v2_fields}
                          : std::span<const std::string_view>{v1_fields};
  std::uint64_t version = 0;
  if (!validate_header(root, "gneiss.texture", fields, 2U, version, diagnostic))
    return diagnostic.result;
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
    const auto usage = yyjson_obj_get(root, "usage");
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

} // namespace

namespace gneiss::render_internal {

gneiss_mesh mesh_asset_lease::get() const noexcept {
  if (entry_ == nullptr || entry_->resource == nullptr) {
    return GNEISS_NULL_MESH;
  }
  return std::static_pointer_cast<mesh_asset>(entry_->resource)->rid;
}

gneiss_material material_asset_lease::get() const noexcept {
  if (entry_ == nullptr || entry_->resource == nullptr) {
    return GNEISS_NULL_MATERIAL;
  }
  return std::static_pointer_cast<material_asset>(entry_->resource)->rid;
}

gneiss_texture texture_asset_lease::get() const noexcept {
  if (entry_ == nullptr || entry_->resource == nullptr) {
    return GNEISS_NULL_TEXTURE;
  }
  return std::static_pointer_cast<texture_asset>(entry_->resource)->rid;
}

render_asset_loader::render_asset_loader(const asset_internal::virtual_file_system& file_system,
                                         asset_internal::resource_cache& cache,
                                         render_resource_service& resources) noexcept
    : file_system_(file_system), cache_(cache), resources_(resources) {}

gneiss_result prepare_render_assets(const asset_internal::virtual_file_system& file_system,
                                    std::span<const render_asset_reload> requested,
                                    prepared_render_batch& output, asset_diagnostic& diagnostic,
                                    const std::function<bool()>& cancelled,
                                    std::size_t maximum_assets,
                                    std::size_t maximum_bytes) noexcept {
  output = {};
  diagnostic = {};
  if (requested.empty() || requested.size() > maximum_assets) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    class source_snapshot final : public asset_internal::file_system {
    public:
      source_snapshot(const asset_internal::virtual_file_system& original, std::size_t limit)
          : original_(original), limit_(limit) {}
      gneiss_result read(std::string_view path,
                         std::vector<std::byte>& data) const noexcept override {
        return read_bounded(path, limit_, data);
      }
      gneiss_result read_bounded(std::string_view path, std::size_t limit,
                                 std::vector<std::byte>& data) const noexcept override {
        try {
          auto found = files_.find(std::string(path));
          if (found == files_.end()) {
            std::vector<std::byte> loaded;
            const auto result = original_.read_bounded("asset://" + std::string(path),
                                                       std::min(limit, limit_ - bytes_), loaded);
            if (result != GNEISS_SUCCESS) {
              return result;
            }
            bytes_ += loaded.size();
            found = files_.emplace(path, std::move(loaded)).first;
          }
          if (found->second.size() > limit) {
            return GNEISS_ERROR_INVALID_ARGUMENT;
          }
          data = found->second;
          return GNEISS_SUCCESS;
        } catch (...) {
          return GNEISS_ERROR_OUT_OF_MEMORY;
        }
      }
      gneiss_result verify(const std::function<bool()>& cancelled) const {
        for (const auto& [path, bytes] : files_) {
          if (cancelled && cancelled()) {
            return GNEISS_ERROR_INVALID_STATE;
          }
          std::vector<std::byte> current;
          if (original_.read_bounded("asset://" + path, limit_, current) != GNEISS_SUCCESS ||
              current != bytes) {
            return GNEISS_ERROR_INVALID_STATE;
          }
        }
        return GNEISS_SUCCESS;
      }
      const asset_internal::virtual_file_system& original_;
      std::size_t limit_;
      mutable std::size_t bytes_{};
      mutable std::map<std::string, std::vector<std::byte>> files_;
    };
    auto snapshot = std::make_shared<source_snapshot>(file_system, maximum_bytes);
    asset_internal::virtual_file_system files;
    auto result = files.mount("asset://", snapshot);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    std::vector<render_asset_reload> pending(requested.begin(), requested.end());
    std::map<std::string, render_asset_type> seen;
    prepared_render_batch batch;
    for (std::size_t index = 0U; index < pending.size(); ++index) {
      if (cancelled && cancelled()) {
        return GNEISS_ERROR_INVALID_STATE;
      }
      const auto source = pending[index];
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
      prepared_render_asset asset;
      asset.source = source;
      if (source.type == render_asset_type::texture) {
        result = prepare_texture(
            files, source.uri, asset.texture, diagnostic,
            std::min(maximum_bytes, (std::size_t{64U} * 1024U * 1024U)), false,
            std::min(maximum_bytes - batch.bytes, (std::size_t{64U} * 1024U * 1024U)));
        asset.bytes = asset.texture.manifest.size() + asset.texture.payload.size();
        for (const auto& mip : asset.texture.levels) {
          asset.bytes += mip.pixels.size();
        }
      } else {
        std::vector<std::byte> bytes;
        result = files.read_bounded(source.uri, maximum_bytes, bytes);
        if (result != GNEISS_SUCCESS) {
          fail(diagnostic, result, source.uri, "无法读取渲染资产源");
          return result;
        }
        if (source.type == render_asset_type::mesh) {
          result = asset_internal::is_mesh_binary(bytes)
                       ? parse_binary_mesh(bytes, asset.mesh.vertices, asset.mesh.normals,
                                           asset.mesh.indices, asset.mesh.tangents, diagnostic)
                       : parse_mesh(bytes, asset.mesh.vertices, asset.mesh.normals, diagnostic);
          // 此校验与 RID 创建的顶点/法线/索引契约一致，在后台完成。
          const auto& mesh = asset.mesh;
          if (result == GNEISS_SUCCESS &&
              (mesh.vertices.size() < 3U ||
               (!mesh.normals.empty() && mesh.normals.size() != mesh.vertices.size()) ||
               (!mesh.indices.empty() &&
                (mesh.indices.size() < 3U || mesh.indices.size() % 3U != 0U)) ||
               !std::ranges::all_of(mesh.vertices,
                                    [](const auto& v) {
                                      return std::isfinite(v.x) && std::isfinite(v.y) &&
                                             std::isfinite(v.z) && std::isfinite(v.u) &&
                                             std::isfinite(v.v);
                                    }) ||
               !std::ranges::all_of(mesh.normals,
                                    [](const auto& n) {
                                      const auto length =
                                          std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
                                      return std::isfinite(length) &&
                                             std::abs(length - 1.0F) <= 1.0e-4F;
                                    }) ||
               !std::ranges::all_of(mesh.indices,
                                    [&](auto i) { return i < mesh.vertices.size(); }))) {
            result = GNEISS_ERROR_INVALID_ARGUMENT;
          }
          asset.bytes = mesh.vertices.size() * sizeof(gneiss_mesh_vertex) +
                        mesh.normals.size() * sizeof(gneiss_mesh_normal) +
                        mesh.indices.size() * sizeof(std::uint32_t) +
                        mesh.tangents.size() * sizeof(gneiss_mesh_tangent);
        } else if (source.type == render_asset_type::material) {
          material_source material;
          result = parse_material(bytes, material, diagnostic);
          asset.material = {material.color[0], material.color[1],   material.color[2],
                            material.color[3], GNEISS_NULL_TEXTURE, material.metallic,
                            material.roughness};
          asset.material.normal_scale = material.normal_scale;
          asset.material.occlusion_strength = material.occlusion_strength;
          asset.material.emissive = material.emissive;
          asset.texture_uris = std::move(material.texture_uris);
          asset.bytes = sizeof(material_resource);
          for (const auto& uri : asset.texture_uris) {
            if (result == GNEISS_SUCCESS && !uri.empty())
              pending.push_back({uri, render_asset_type::texture});
            asset.bytes += uri.size();
          }
        } else {
          result = GNEISS_ERROR_INVALID_ARGUMENT;
        }
      }
      if (result != GNEISS_SUCCESS) {
        return result;
      }
      if (asset.bytes > maximum_bytes - batch.bytes) {
        fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, source.uri, "渲染资产候选超过字节预算");
        return GNEISS_ERROR_INVALID_ARGUMENT;
      }
      batch.bytes += asset.bytes;
      batch.assets.push_back(std::move(asset));
    }
    result = snapshot->verify(cancelled);
    if (result != GNEISS_SUCCESS) {
      fail(diagnostic, result, "", "准备期间源变化或请求取消");
      return result;
    }
    std::stable_sort(batch.assets.begin(), batch.assets.end(), [](const auto& a, const auto& b) {
      const auto order = [](auto type) {
        return type == render_asset_type::texture ? 0 : type == render_asset_type::mesh ? 1 : 2;
      };
      return order(a.source.type) < order(b.source.type);
    });
    batch.input_bytes = snapshot->bytes_;
    output = std::move(batch);
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

std::uint64_t render_asset_lease::get() const noexcept {
  if (!entry_ || !entry_->resource) {
    return 0U;
  }
  switch (type()) {
  case render_asset_type::invalid:
    return 0U;
  case render_asset_type::mesh:
    return std::static_pointer_cast<mesh_asset>(entry_->resource)->rid;
  case render_asset_type::material:
    return std::static_pointer_cast<material_asset>(entry_->resource)->rid;
  case render_asset_type::texture:
    return std::static_pointer_cast<texture_asset>(entry_->resource)->rid;
  }
  return 0U;
}
render_asset_type render_asset_lease::type() const noexcept {
  return entry_ ? static_cast<render_asset_type>(entry_->type) : render_asset_type::invalid;
}
texture_asset_lease
render_asset_loader::texture_lease(const render_asset_lease& lease) const noexcept {
  texture_asset_lease result;
  if (lease.type() == render_asset_type::texture) {
    result.entry_ = lease.entry_;
  }
  return result;
}

gneiss_result render_asset_loader::acquire_cached(const render_asset_reload& source,
                                                  render_asset_lease& output) const noexcept {
  output = {};
  try {
    const auto current = cache_.observe(source.uri).lock();
    if (!current) {
      return GNEISS_ERROR_NOT_FOUND;
    }
    if (current->type != static_cast<std::uint32_t>(source.type) ||
        current->state != asset_internal::resource_state::ready) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    output.entry_ = current;
    const auto rid = output.get();
    if ((source.type == render_asset_type::texture && resources_.get_texture(rid)) ||
        (source.type == render_asset_type::mesh && resources_.get_mesh(rid)) ||
        (source.type == render_asset_type::material && resources_.get_material(rid))) {
      return GNEISS_SUCCESS;
    }
    output = {};
    return GNEISS_ERROR_INVALID_HANDLE;
  } catch (...) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  }
}

gneiss_result render_asset_loader::stage_asset(prepared_render_asset prepared,
                                               std::span<const asset_candidate> staged,
                                               asset_candidate& output) noexcept {
  output = {};
  try {
    asset_candidate candidate;
    candidate.source = prepared.source;
    candidate.bytes = prepared.bytes;
    const auto current = cache_.observe(prepared.source.uri).lock();
    if (current && (current->type != static_cast<std::uint32_t>(prepared.source.type) ||
                    current->state != asset_internal::resource_state::ready)) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    candidate.existed = static_cast<bool>(current);
    candidate.expected = current;
    std::shared_ptr<void> owned;
    std::uint64_t rid{};
    auto created = GNEISS_ERROR_INVALID_ARGUMENT;
    if (prepared.source.type == render_asset_type::texture) {
      created = prepared.texture.manifest.empty()
                    ? resources_.create_texture(std::move(prepared.texture), &rid)
                    : resources_.create_packaged_texture(std::move(prepared.texture), &rid);
      if (created != GNEISS_SUCCESS) {
        return created;
      }
      try {
        owned = std::make_shared<texture_asset>(resources_, rid);
      } catch (...) {
        (void)resources_.destroy_texture(rid);
        throw;
      }
      candidate.texture = resources_.share_texture(rid);
    } else if (prepared.source.type == render_asset_type::mesh) {
      created = resources_.create_prepared_mesh(std::move(prepared.mesh), &rid);
      if (created != GNEISS_SUCCESS) {
        return created;
      }
      try {
        owned = std::make_shared<mesh_asset>(resources_, rid);
      } catch (...) {
        (void)resources_.destroy_mesh(rid);
        throw;
      }
      candidate.mesh = resources_.share_mesh(rid);
    } else if (prepared.source.type == render_asset_type::material) {
      for (std::size_t slot = 0; slot < prepared.texture_uris.size(); ++slot) {
        const auto& uri = prepared.texture_uris[slot];
        if (uri.empty())
          continue;
        const auto found = std::ranges::find_if(staged, [&](const auto& other) {
          return other.source.uri == uri && other.source.type == render_asset_type::texture;
        });
        if (found == staged.end())
          return GNEISS_ERROR_INVALID_STATE;
        prepared.material.set_texture(slot, found->lease.get());
        candidate.dependencies[slot] = found->lease.entry_;
        candidate.dependency_textures[slot] = found->texture;
      }
      const auto desc = prepared.material.description();
      created = resources_.create_material(desc, &rid);
      if (created != GNEISS_SUCCESS) {
        return created;
      }
      try {
        owned = std::make_shared<material_asset>(resources_, rid, candidate.dependencies);
      } catch (...) {
        (void)resources_.destroy_material(rid);
        throw;
      }
      candidate.material = resources_.share_material(rid);
    } else {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    if (current) {
      candidate.lease.entry_ = current;
      const auto existing = candidate.lease.get();
      switch (prepared.source.type) {
      case render_asset_type::invalid:
        return GNEISS_ERROR_INVALID_ARGUMENT;
      case render_asset_type::texture:
        candidate.previous = resources_.share_texture(existing);
        break;
      case render_asset_type::mesh:
        candidate.previous = resources_.share_mesh(existing);
        break;
      case render_asset_type::material:
        candidate.previous = resources_.share_material(existing);
        break;
      }
      if (!candidate.previous) {
        return GNEISS_ERROR_INVALID_HANDLE;
      }
    } else {
      auto entry = std::make_shared<asset_internal::resource_cache::entry>();
      entry->uri = prepared.source.uri;
      entry->type = static_cast<std::uint32_t>(prepared.source.type);
      entry->state = asset_internal::resource_state::ready;
      entry->resource = std::move(owned);
      candidate.lease.entry_ = std::move(entry);
    }
    output = std::move(candidate);
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result render_asset_loader::publish_assets(std::span<asset_candidate> candidates) noexcept {
  try {
    std::vector<asset_internal::resource_cache::reload_request> requests;
    for (const auto& candidate : candidates) {
      const auto current = cache_.observe(candidate.source.uri).lock();
      std::shared_ptr<const void> previous;
      if (candidate.texture) {
        previous = resources_.share_texture(candidate.lease.get());
      }
      if (candidate.mesh) {
        previous = resources_.share_mesh(candidate.lease.get());
      }
      if (candidate.material) {
        previous = resources_.share_material(candidate.lease.get());
      }
      if (!candidate.lease || !previous ||
          (candidate.existed &&
           (!current || current != candidate.expected.lock() || previous != candidate.previous)) ||
          (!candidate.existed && current)) {
        return GNEISS_ERROR_INVALID_STATE;
      }
      if (!candidate.existed) {
        requests.push_back(
            {.uri = candidate.source.uri,
             .type = static_cast<std::uint32_t>(candidate.source.type),
             .load = [resource = candidate.lease.entry_->resource](auto&, auto& output) {
               output = resource;
               return GNEISS_SUCCESS;
             }});
      }
    }
    std::vector<std::shared_ptr<const asset_internal::resource_cache::entry>> committed;
    const auto result =
        requests.empty() ? GNEISS_SUCCESS : cache_.reload_transaction(requests, committed);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    std::size_t inserted{};
    for (auto& candidate : candidates) {
      if (!candidate.existed) {
        candidate.lease.entry_ = committed[inserted++];
      }
    }
    // 所有分配和校验已经完成，以下共享指针交换不会失败。
    for (auto& candidate : candidates) {
      const auto rid = candidate.lease.get();
      if (candidate.texture) {
        (void)resources_.replace_texture(rid, candidate.texture);
      }
      if (candidate.mesh) {
        (void)resources_.replace_mesh(rid, candidate.mesh);
      }
      if (candidate.material) {
        auto dependencies = candidate.dependencies;
        for (auto& dependency : dependencies) {
          if (!dependency)
            continue;
          const auto found = std::ranges::find_if(candidates, [&](const auto& other) {
            return other.source.uri == dependency->uri && other.texture;
          });
          if (found != candidates.end())
            dependency = found->lease.entry_;
        }
        std::static_pointer_cast<material_asset>(candidate.lease.entry_->resource)->textures =
            std::move(dependencies);
        (void)resources_.replace_material(rid, candidate.material);
      }
    }
    ++revision_;
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result render_asset_loader::acquire_mesh(std::string_view uri, mesh_asset_lease& out_lease,
                                                asset_diagnostic& out_diagnostic) noexcept {
  out_lease = {};
  out_diagnostic = {};
  const auto result = cache_.acquire(
      uri, mesh_type,
      [this, uri, &out_diagnostic](std::shared_ptr<void>& output) -> gneiss_result {
        std::vector<std::byte> bytes;
        auto result = file_system_.read(uri, bytes);
        if (result != GNEISS_SUCCESS) {
          fail(out_diagnostic, result, "", "无法通过 VFS 读取 Mesh");
          return result;
        }
        std::vector<gneiss_mesh_vertex> vertices;
        std::vector<gneiss_mesh_normal> normals;
        std::vector<std::uint32_t> indices;
        std::vector<gneiss_mesh_tangent> tangents;
        result =
            gneiss::asset_internal::is_mesh_binary(bytes)
                ? parse_binary_mesh(bytes, vertices, normals, indices, tangents, out_diagnostic)
                : parse_mesh(bytes, vertices, normals, out_diagnostic);
        if (result != GNEISS_SUCCESS) {
          return result;
        }
        const gneiss_mesh_desc desc{.struct_size = sizeof(gneiss_mesh_desc),
                                    .vertex_count = static_cast<std::uint32_t>(vertices.size()),
                                    .vertices = vertices.data(),
                                    .reserved = 0,
                                    .reserved_2 = 0,
                                    .normal_count = static_cast<std::uint32_t>(normals.size()),
                                    .normals = normals.empty() ? nullptr : normals.data(),
                                    .index_count = static_cast<std::uint32_t>(indices.size()),
                                    .reserved_3 = 0,
                                    .indices = indices.empty() ? nullptr : indices.data(),
                                    .tangent_count = static_cast<std::uint32_t>(tangents.size()),
                                    .reserved_4 = 0U,
                                    .tangents = tangents.empty() ? nullptr : tangents.data()};
        gneiss_mesh rid = GNEISS_NULL_MESH;
        result = resources_.create_mesh(desc, &rid);
        if (result != GNEISS_SUCCESS) {
          fail(out_diagnostic, result, "", "创建 Mesh RID 失败");
          return result;
        }
        try {
          output = std::make_shared<mesh_asset>(resources_, rid);
        } catch (...) {
          (void)resources_.destroy_mesh(rid);
          throw;
        }
        return GNEISS_SUCCESS;
      },
      out_lease.entry_);
  if (result != GNEISS_SUCCESS && out_diagnostic.result == GNEISS_SUCCESS) {
    fail(out_diagnostic, result, "", "获取 Mesh 资产失败");
  }
  return result;
}

gneiss_result render_asset_loader::acquire_material(std::string_view uri,
                                                    material_asset_lease& out_lease,
                                                    asset_diagnostic& out_diagnostic) noexcept {
  out_lease = {};
  out_diagnostic = {};
  const auto result = cache_.acquire(
      uri, material_type,
      [this, uri, &out_diagnostic](std::shared_ptr<void>& output) -> gneiss_result {
        std::vector<std::byte> bytes;
        auto result = file_system_.read(uri, bytes);
        if (result != GNEISS_SUCCESS) {
          fail(out_diagnostic, result, "", "无法通过 VFS 读取 Material");
          return result;
        }
        material_source source;
        result = parse_material(bytes, source, out_diagnostic);
        if (result != GNEISS_SUCCESS) {
          return result;
        }
        material_resource material{source.color[0], source.color[1],     source.color[2],
                                   source.color[3], GNEISS_NULL_TEXTURE, source.metallic,
                                   source.roughness};
        material.normal_scale = source.normal_scale;
        material.occlusion_strength = source.occlusion_strength;
        material.emissive = source.emissive;
        std::array<std::shared_ptr<const asset_internal::resource_cache::entry>, 5> dependencies;
        constexpr std::array names{"base_color_texture", "metallic_roughness_texture",
                                   "normal_texture", "occlusion_texture", "emissive_texture"};
        for (std::size_t slot = 0; slot < source.texture_uris.size(); ++slot) {
          if (source.texture_uris[slot].empty())
            continue;
          texture_asset_lease texture;
          asset_diagnostic texture_diagnostic;
          result = acquire_texture(source.texture_uris[slot], texture, texture_diagnostic);
          if (result != GNEISS_SUCCESS) {
            fail(out_diagnostic, result, std::string{"/"} + names[slot],
                 texture_diagnostic.message);
            return result;
          }
          material.set_texture(slot, texture.get());
          dependencies[slot] = texture.entry_;
        }
        const auto desc = material.description();
        gneiss_material rid = GNEISS_NULL_MATERIAL;
        result = resources_.create_material(desc, &rid);
        if (result != GNEISS_SUCCESS) {
          fail(out_diagnostic, result, "", "创建 Material RID 失败");
          return result;
        }
        try {
          output = std::make_shared<material_asset>(resources_, rid, std::move(dependencies));
        } catch (...) {
          (void)resources_.destroy_material(rid);
          throw;
        }
        return GNEISS_SUCCESS;
      },
      out_lease.entry_);
  if (result != GNEISS_SUCCESS && out_diagnostic.result == GNEISS_SUCCESS) {
    fail(out_diagnostic, result, "", "获取 Material 资产失败");
  }
  return result;
}

gneiss_result prepare_texture(const asset_internal::virtual_file_system& file_system,
                              std::string_view uri, texture_resource& output,
                              asset_diagnostic& out_diagnostic, std::size_t input_limit,
                              bool verify_source, std::size_t output_limit) noexcept {
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
    result = file_system.read_bounded(source.uri,
                                      input_limit == std::numeric_limits<std::size_t>::max()
                                          ? input_limit
                                          : std::min(input_limit, output_limit),
                                      image_bytes);
    if (result != GNEISS_SUCCESS) {
      fail(out_diagnostic, result, "/source", "无法通过 VFS 读取纹理数据");
      return result;
    }
    if (std::string_view(source.uri).ends_with(".gneiss-texture")) {
#if defined(GNEISS_HAS_GRANIT_PLATFORM)
      asset_internal::texture_binary_view binary;
      std::string decode_message;
      if (asset_internal::decode_texture_binary(image_bytes, binary, decode_message) !=
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
      const auto payload_in_bounds = [&binary](const auto& variant) {
        return variant.payload_offset <= binary.payload.size() &&
               variant.payload_size <= binary.payload.size() - variant.payload_offset;
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
      output = {.width = info.width,
                .height = info.height,
                .format = GNEISS_TEXTURE_FORMAT_RGBA8_UNORM,
                .color_space = source.color_space,
                .levels = {},
                .manifest = std::vector<std::byte>(binary.manifest.begin(), binary.manifest.end()),
                .payload = std::vector<std::byte>(binary.payload.begin(), binary.payload.end())};
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
      decoded_png image;
      std::string decode_message;
      result = decode_png(image_bytes, image, decode_message, output_limit);
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
          file_system.read_bounded(source.uri, input_limit, checked) != GNEISS_SUCCESS ||
          checked != image_bytes) {
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

gneiss_result render_asset_loader::acquire_texture(std::string_view uri,
                                                   texture_asset_lease& out_lease,
                                                   asset_diagnostic& out_diagnostic) noexcept {
  out_lease = {};
  out_diagnostic = {};
  const auto result = cache_.acquire(
      uri, texture_type,
      [this, uri, &out_diagnostic](std::shared_ptr<void>& output) -> gneiss_result {
        texture_resource prepared;
        auto result = prepare_texture(file_system_, uri, prepared, out_diagnostic);
        if (result != GNEISS_SUCCESS) {
          return result;
        }
        gneiss_texture rid = GNEISS_NULL_TEXTURE;
        result = prepared.manifest.empty()
                     ? resources_.create_texture(std::move(prepared), &rid)
                     : resources_.create_packaged_texture(std::move(prepared), &rid);
        if (result != GNEISS_SUCCESS) {
          fail(out_diagnostic, result, "", "创建 Texture RID 失败");
          return result;
        }
        try {
          output = std::make_shared<texture_asset>(resources_, rid);
        } catch (...) {
          (void)resources_.destroy_texture(rid);
          throw;
        }
        return GNEISS_SUCCESS;
      },
      out_lease.entry_);
  if (result != GNEISS_SUCCESS && out_diagnostic.result == GNEISS_SUCCESS) {
    fail(out_diagnostic, result, "", "获取 Texture 资产失败");
  }
  return result;
}

gneiss_result render_asset_loader::observe_texture(std::string_view uri,
                                                   texture_target& output) const noexcept {
  output = {};
  try {
    output.uri = uri;
    output.expected = cache_.observe(uri);
    const auto current = output.expected.lock();
    output.existed = static_cast<bool>(current);
    return current && (current->type != texture_type ||
                       current->state != asset_internal::resource_state::ready)
               ? GNEISS_ERROR_INVALID_ARGUMENT
               : GNEISS_SUCCESS;
  } catch (...) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  }
}

gneiss_result render_asset_loader::stage_texture(const texture_target& target,
                                                 texture_resource prepared,
                                                 texture_candidate& output) noexcept {
  output = {};
  try {
    const auto current = cache_.observe(target.uri).lock();
    if ((target.existed && (!current || current != target.expected.lock())) ||
        (!target.existed && current)) {
      return GNEISS_ERROR_INVALID_STATE;
    }
    texture_candidate candidate;
    candidate.target = target;
    gneiss_texture staged = GNEISS_NULL_TEXTURE;
    const auto created = prepared.manifest.empty()
                             ? resources_.create_texture(std::move(prepared), &staged)
                             : resources_.create_packaged_texture(std::move(prepared), &staged);
    if (created != GNEISS_SUCCESS) {
      return created;
    }
    // 私有暂存 RID 只用于验证和所有权；失败时不会进入缓存。
    std::shared_ptr<texture_asset> owned;
    try {
      owned = std::make_shared<texture_asset>(resources_, staged);
    } catch (...) {
      (void)resources_.destroy_texture(staged);
      throw;
    }
    candidate.data = resources_.share_texture(staged);
    if (current) {
      candidate.lease.entry_ = current;
      candidate.previous = resources_.share_texture(candidate.lease.get());
      if (!candidate.previous) {
        return GNEISS_ERROR_INVALID_HANDLE;
      }
    } else {
      auto entry = std::make_shared<asset_internal::resource_cache::entry>();
      entry->uri = target.uri;
      entry->type = texture_type;
      entry->state = asset_internal::resource_state::ready;
      entry->resource = std::move(owned);
      candidate.lease.entry_ = std::move(entry);
    }
    output = std::move(candidate);
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result
render_asset_loader::publish_textures(std::span<texture_candidate> candidates) noexcept {
  try {
    std::vector<asset_internal::resource_cache::reload_request> requests;
    requests.reserve(candidates.size());
    for (const auto& candidate : candidates) {
      const auto current = cache_.observe(candidate.target.uri).lock();
      if (!candidate.data || !candidate.lease ||
          (candidate.target.existed &&
           (!current || current != candidate.target.expected.lock() ||
            resources_.share_texture(candidate.lease.get()) != candidate.previous)) ||
          (!candidate.target.existed && current) ||
          !resources_.get_texture(candidate.lease.get())) {
        return GNEISS_ERROR_INVALID_STATE;
      }
      if (candidate.target.existed) {
        continue;
      }
      requests.push_back(
          {.uri = candidate.target.uri,
           .type = texture_type,
           .load = [resource = candidate.lease.entry_->resource](auto&, auto& output) {
             output = resource;
             return GNEISS_SUCCESS;
           }});
    }
    std::vector<std::shared_ptr<const asset_internal::resource_cache::entry>> committed;
    const auto result =
        requests.empty() ? GNEISS_SUCCESS : cache_.reload_transaction(requests, committed);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    ++revision_;
    // 缓存事务已完成全部可能分配的工作；这里仅交换已验证槽位的 shared_ptr，不会失败。
    std::size_t inserted{};
    for (auto& candidate : candidates) {
      if (!candidate.target.existed) {
        candidate.lease.entry_ = committed[inserted++];
      }
      (void)resources_.replace_texture(candidate.lease.get(), candidate.data);
    }
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result render_asset_loader::reload_assets(std::span<const render_asset_reload> assets,
                                                 asset_diagnostic& out_diagnostic) noexcept {
  out_diagnostic = {};
  if (assets.empty()) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    std::vector<render_asset_reload> ordered(assets.begin(), assets.end());
    const auto priority = [](render_asset_type type) {
      switch (type) {
      case render_asset_type::invalid:
        return 3U;
      case render_asset_type::texture:
        return 0U;
      case render_asset_type::material:
        return 1U;
      case render_asset_type::mesh:
        return 2U;
      }
      return 3U;
    };
    std::stable_sort(ordered.begin(), ordered.end(), [&](const auto& left, const auto& right) {
      return priority(left.type) < priority(right.type);
    });
    std::vector<asset_internal::resource_cache::reload_request> requests;
    requests.reserve(ordered.size());
    for (const auto& asset : ordered) {
      if (priority(asset.type) == 3U) {
        return GNEISS_ERROR_INVALID_ARGUMENT;
      }
      requests.push_back({.uri = asset.uri,
                          .type = static_cast<std::uint32_t>(asset.type),
                          .load = [this, uri = asset.uri, type = asset.type, &out_diagnostic](
                                      asset_internal::resource_cache& staging,
                                      std::shared_ptr<void>& output) -> gneiss_result {
                            render_asset_loader loader(file_system_, staging, resources_);
                            gneiss_result result = GNEISS_ERROR_INVALID_ARGUMENT;
                            if (type == render_asset_type::texture) {
                              texture_asset_lease lease;
                              result = loader.acquire_texture(uri, lease, out_diagnostic);
                              if (result == GNEISS_SUCCESS) {
                                output = lease.entry_->resource;
                              }
                            } else if (type == render_asset_type::material) {
                              material_asset_lease lease;
                              result = loader.acquire_material(uri, lease, out_diagnostic);
                              if (result == GNEISS_SUCCESS) {
                                output = lease.entry_->resource;
                              }
                            } else if (type == render_asset_type::mesh) {
                              mesh_asset_lease lease;
                              result = loader.acquire_mesh(uri, lease, out_diagnostic);
                              if (result == GNEISS_SUCCESS) {
                                output = lease.entry_->resource;
                              }
                            }
                            return result;
                          }});
    }
    std::vector<std::shared_ptr<const asset_internal::resource_cache::entry>> committed;
    const auto result = cache_.reload_transaction(requests, committed);
    if (result != GNEISS_SUCCESS && out_diagnostic.result == GNEISS_SUCCESS) {
      fail(out_diagnostic, result, "", "渲染资产事务重载失败");
    }
    return result;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

} // namespace gneiss::render_internal
