// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "render/render_asset_loader.h"

#include "asset/virtual_file_system.h"
#include "render/png_decoder.h"
#include "render/render_resource_service.h"

#include <yyjson.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
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
  material_asset(gneiss::render_internal::render_resource_service& owner,
                 gneiss_material value) noexcept
      : resources(&owner), rid(value) {}
  material_asset(const material_asset&) = delete;
  material_asset& operator=(const material_asset&) = delete;
  gneiss::render_internal::render_resource_service* resources;
  gneiss_material rid;
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
  if (!yyjson_is_uint(version) || yyjson_get_uint(version) != 1U) {
    fail(diagnostic,
         yyjson_is_uint(version) && yyjson_get_uint(version) > 1U ? GNEISS_ERROR_UNSUPPORTED
                                                                  : GNEISS_ERROR_INVALID_ARGUMENT,
         "/version", "不支持的资产版本");
    return false;
  }
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

[[nodiscard]] gneiss_result parse_mesh(const std::vector<std::byte>& bytes,
                                       std::vector<gneiss_mesh_vertex>& out_vertices,
                                       asset_diagnostic& diagnostic) {
  auto document = parse_document(bytes, diagnostic);
  if (!document) {
    return diagnostic.result;
  }
  yyjson_val* root = yyjson_doc_get_root(document.get());
  constexpr std::array fields{std::string_view{"format"}, std::string_view{"version"},
                              std::string_view{"topology"}, std::string_view{"vertices"}};
  if (!validate_header(root, "gneiss.mesh", fields, diagnostic)) {
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
  out_vertices.reserve(count);
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
    out_vertices.push_back(parsed);
  }
  return GNEISS_SUCCESS;
}

[[nodiscard]] gneiss_result parse_material(const std::vector<std::byte>& bytes,
                                           std::array<float, 4>& out_color,
                                           asset_diagnostic& diagnostic) {
  auto document = parse_document(bytes, diagnostic);
  if (!document) {
    return diagnostic.result;
  }
  yyjson_val* root = yyjson_doc_get_root(document.get());
  constexpr std::array fields{std::string_view{"format"}, std::string_view{"version"},
                              std::string_view{"color"}};
  if (!validate_header(root, "gneiss.material", fields, diagnostic)) {
    return diagnostic.result;
  }
  yyjson_val* color = yyjson_obj_get(root, "color");
  if (!yyjson_is_arr(color) || yyjson_arr_size(color) != out_color.size()) {
    fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "/color", "颜色必须包含四个分量");
    return diagnostic.result;
  }
  for (std::size_t index = 0; index < out_color.size(); ++index) {
    if (!read_float(yyjson_arr_get(color, index), out_color[index]) || out_color[index] < 0.0F ||
        out_color[index] > 1.0F) {
      fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "/color/" + std::to_string(index),
           "颜色分量必须位于 0..1");
      return diagnostic.result;
    }
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
  constexpr std::array fields{std::string_view{"format"}, std::string_view{"version"},
                              std::string_view{"source"}, std::string_view{"color_space"}};
  if (!validate_header(root, "gneiss.texture", fields, diagnostic)) {
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
        result = parse_mesh(bytes, vertices, out_diagnostic);
        if (result != GNEISS_SUCCESS) {
          return result;
        }
        const gneiss_mesh_desc desc{.struct_size = sizeof(gneiss_mesh_desc),
                                    .vertex_count = static_cast<std::uint32_t>(vertices.size()),
                                    .vertices = vertices.data(),
                                    .reserved = 0};
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
        std::array<float, 4> color{};
        result = parse_material(bytes, color, out_diagnostic);
        if (result != GNEISS_SUCCESS) {
          return result;
        }
        const gneiss_material_desc desc{.struct_size = sizeof(gneiss_material_desc),
                                        .reserved = 0,
                                        .red = color[0],
                                        .green = color[1],
                                        .blue = color[2],
                                        .alpha = color[3]};
        gneiss_material rid = GNEISS_NULL_MATERIAL;
        result = resources_.create_material(desc, &rid);
        if (result != GNEISS_SUCCESS) {
          fail(out_diagnostic, result, "", "创建 Material RID 失败");
          return result;
        }
        try {
          output = std::make_shared<material_asset>(resources_, rid);
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

gneiss_result render_asset_loader::acquire_texture(std::string_view uri,
                                                   texture_asset_lease& out_lease,
                                                   asset_diagnostic& out_diagnostic) noexcept {
  out_lease = {};
  out_diagnostic = {};
  const auto result = cache_.acquire(
      uri, texture_type,
      [this, uri, &out_diagnostic](std::shared_ptr<void>& output) -> gneiss_result {
        std::vector<std::byte> description_bytes;
        auto result = file_system_.read(uri, description_bytes);
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
        result = file_system_.read(source.uri, image_bytes);
        if (result != GNEISS_SUCCESS) {
          fail(out_diagnostic, result, "/source", "无法通过 VFS 读取 PNG");
          return result;
        }
        decoded_png image;
        std::string decode_message;
        result = decode_png(image_bytes, image, decode_message);
        if (result != GNEISS_SUCCESS) {
          fail(out_diagnostic, result, "/source",
               decode_message.empty() ? "PNG 解码失败" : decode_message);
          return result;
        }
        const gneiss_texture_desc desc{
            .struct_size = sizeof(gneiss_texture_desc),
            .format = GNEISS_TEXTURE_FORMAT_RGBA8_UNORM,
            .color_space = source.color_space,
            .width = image.width,
            .height = image.height,
            .row_stride_bytes = image.width * 4U,
            .pixel_data_size = image.pixels.size(),
            .pixels = reinterpret_cast<const std::uint8_t*>(image.pixels.data()),
            .reserved = {0, 0}};
        gneiss_texture rid = GNEISS_NULL_TEXTURE;
        result = resources_.create_texture(desc, &rid);
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

} // namespace gneiss::render_internal
