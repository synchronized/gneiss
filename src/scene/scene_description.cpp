// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "scene/scene_description.h"

#include "asset/asset_uri.h"
#include "asset/virtual_file_system.h"

#include <yyjson.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <numbers>
#include <numeric>
#include <span>
#include <unordered_map>

namespace {

using gneiss::scene_internal::scene_diagnostic;

constexpr std::uint64_t schema_version = 1;
constexpr std::string_view scene_format = "gneiss.scene";

void fail(scene_diagnostic& diagnostic, gneiss_result result, std::string_view path,
          std::string_view message, std::size_t byte_offset = 0) noexcept {
  diagnostic.result = result;
  diagnostic.byte_offset = byte_offset;
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
                                   std::string_view path, scene_diagnostic& diagnostic) {
  yyjson_val* key = nullptr;
  yyjson_val* value = nullptr;
  std::size_t index = 0;
  std::size_t maximum = 0;
  yyjson_obj_foreach(object, index, maximum, key, value) {
    (void)value;
    const auto name = json_string(key);
    if (std::ranges::find(fields, name) == fields.end()) {
      fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, std::string(path) + "/" + std::string(name),
           "未知字段");
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool read_required_string(yyjson_val* object, const char* name, std::string_view path,
                                        std::string& output, scene_diagnostic& diagnostic) {
  yyjson_val* value = yyjson_obj_get(object, name);
  const std::string field_path = std::string(path) + "/" + name;
  if (!yyjson_is_str(value) || yyjson_get_len(value) == 0U) {
    fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, field_path, "字段必须是非空字符串");
    return false;
  }
  output = json_string(value);
  return true;
}

[[nodiscard]] bool is_canonical_uuid(std::string_view value) {
  if (value.size() != 36U) {
    return false;
  }
  for (std::size_t index = 0; index < value.size(); ++index) {
    if (index == 8U || index == 13U || index == 18U || index == 23U) {
      if (value[index] != '-') {
        return false;
      }
    } else if ((value[index] < '0' || value[index] > '9') &&
               (value[index] < 'a' || value[index] > 'f')) {
      return false;
    }
  }
  return true;
}

template <std::size_t Size>
[[nodiscard]] bool read_float_array(yyjson_val* object, const char* name, std::string_view path,
                                    std::array<float, Size>& output, scene_diagnostic& diagnostic) {
  yyjson_val* array = yyjson_obj_get(object, name);
  const std::string field_path = std::string(path) + "/" + name;
  if (!yyjson_is_arr(array) || yyjson_arr_size(array) != Size) {
    fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, field_path, "数组长度或类型错误");
    return false;
  }
  for (std::size_t index = 0; index < Size; ++index) {
    yyjson_val* value = yyjson_arr_get(array, index);
    const double number = yyjson_get_num(value);
    if (!yyjson_is_num(value) || !std::isfinite(number) ||
        std::abs(number) > std::numeric_limits<float>::max()) {
      fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, field_path + "/" + std::to_string(index),
           "数值必须是有限 float");
      return false;
    }
    output[index] = static_cast<float>(number);
  }
  return true;
}

[[nodiscard]] bool read_float(yyjson_val* object, const char* name, std::string_view path,
                              float& output, scene_diagnostic& diagnostic) {
  yyjson_val* value = yyjson_obj_get(object, name);
  const double number = yyjson_get_num(value);
  if (!yyjson_is_num(value) || !std::isfinite(number) ||
      std::abs(number) > std::numeric_limits<float>::max()) {
    fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, std::string(path) + "/" + name,
         "数值必须是有限 float");
    return false;
  }
  output = static_cast<float>(number);
  return true;
}

[[nodiscard]] bool parse_transform(yyjson_val* value, std::string_view path,
                                   gneiss::scene_internal::object_description& output,
                                   scene_diagnostic& diagnostic) {
  constexpr std::array fields{std::string_view{"translation"}, std::string_view{"rotation"},
                              std::string_view{"scale"}};
  if (!yyjson_is_obj(value) || !has_only_fields(value, fields, path, diagnostic) ||
      !read_float_array(value, "translation", path, output.translation, diagnostic) ||
      !read_float_array(value, "rotation", path, output.rotation, diagnostic) ||
      !read_float_array(value, "scale", path, output.scale, diagnostic)) {
    if (diagnostic.result == GNEISS_SUCCESS) {
      fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, path, "transform 必须是对象");
    }
    return false;
  }
  const double rotation_length = std::sqrt(std::inner_product(
      output.rotation.begin(), output.rotation.end(), output.rotation.begin(), 0.0));
  if (std::abs(rotation_length - 1.0) > 1.0e-4 ||
      std::ranges::any_of(output.scale, [](float value) { return std::abs(value) < 1.0e-6F; })) {
    fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, path, "旋转必须归一化且缩放分量不能为零");
    return false;
  }
  return true;
}

[[nodiscard]] bool parse_camera(yyjson_val* value, std::string_view path,
                                gneiss::scene_internal::camera_description& output,
                                scene_diagnostic& diagnostic) {
  constexpr std::array fields{std::string_view{"vertical_field_of_view_radians"},
                              std::string_view{"near_plane"}, std::string_view{"far_plane"},
                              std::string_view{"primary"}};
  if (!yyjson_is_obj(value) || !has_only_fields(value, fields, path, diagnostic) ||
      !read_float(value, "vertical_field_of_view_radians", path,
                  output.vertical_field_of_view_radians, diagnostic) ||
      !read_float(value, "near_plane", path, output.near_plane, diagnostic) ||
      !read_float(value, "far_plane", path, output.far_plane, diagnostic)) {
    if (diagnostic.result == GNEISS_SUCCESS) {
      fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, path, "camera 必须是对象");
    }
    return false;
  }
  yyjson_val* primary = yyjson_obj_get(value, "primary");
  if (!yyjson_is_bool(primary)) {
    fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, std::string(path) + "/primary",
         "primary 必须是布尔值");
    return false;
  }
  output.is_primary = yyjson_get_bool(primary);
  if (output.vertical_field_of_view_radians <= 0.0F ||
      output.vertical_field_of_view_radians >= std::numbers::pi_v<float> ||
      output.near_plane <= 0.0F || output.far_plane <= output.near_plane) {
    fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, path, "相机透视参数范围错误");
    return false;
  }
  return true;
}

[[nodiscard]] bool parse_mesh_renderer(yyjson_val* value, std::string_view path,
                                       gneiss::scene_internal::mesh_renderer_description& output,
                                       scene_diagnostic& diagnostic) {
  constexpr std::array fields{std::string_view{"mesh"}, std::string_view{"material"}};
  if (!yyjson_is_obj(value) || !has_only_fields(value, fields, path, diagnostic) ||
      !read_required_string(value, "mesh", path, output.mesh_uri, diagnostic) ||
      !read_required_string(value, "material", path, output.material_uri, diagnostic)) {
    if (diagnostic.result == GNEISS_SUCCESS) {
      fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, path, "mesh_renderer 必须是对象");
    }
    return false;
  }
  if (gneiss::asset_internal::validate_uri(output.mesh_uri) != GNEISS_SUCCESS ||
      gneiss::asset_internal::validate_uri(output.material_uri) != GNEISS_SUCCESS) {
    fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, path, "资源引用必须是规范 asset URI");
    return false;
  }
  return true;
}

[[nodiscard]] bool parse_components(yyjson_val* value, std::string_view path,
                                    gneiss::scene_internal::object_description& output,
                                    scene_diagnostic& diagnostic) {
  constexpr std::array fields{std::string_view{"camera"}, std::string_view{"mesh_renderer"}};
  if (!yyjson_is_obj(value) || !has_only_fields(value, fields, path, diagnostic)) {
    if (diagnostic.result == GNEISS_SUCCESS) {
      fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, path, "components 必须是对象");
    }
    return false;
  }
  if (yyjson_val* camera = yyjson_obj_get(value, "camera"); camera != nullptr) {
    output.camera.emplace();
    if (!parse_camera(camera, std::string(path) + "/camera", *output.camera, diagnostic)) {
      return false;
    }
  }
  if (yyjson_val* renderer = yyjson_obj_get(value, "mesh_renderer"); renderer != nullptr) {
    output.mesh_renderer.emplace();
    if (!parse_mesh_renderer(renderer, std::string(path) + "/mesh_renderer", *output.mesh_renderer,
                             diagnostic)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool parse_object(yyjson_val* value, std::size_t index,
                                gneiss::scene_internal::object_description& output,
                                scene_diagnostic& diagnostic) {
  const std::string path = "/objects/" + std::to_string(index);
  constexpr std::array fields{std::string_view{"uuid"}, std::string_view{"parent"},
                              std::string_view{"transform"}, std::string_view{"components"}};
  if (!yyjson_is_obj(value) || !has_only_fields(value, fields, path, diagnostic) ||
      !read_required_string(value, "uuid", path, output.uuid, diagnostic)) {
    if (diagnostic.result == GNEISS_SUCCESS) {
      fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, path, "对象描述类型错误");
    }
    return false;
  }
  if (!is_canonical_uuid(output.uuid)) {
    fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, path + "/uuid", "UUID 必须是小写规范形式");
    return false;
  }
  yyjson_val* parent = yyjson_obj_get(value, "parent");
  if (parent == nullptr) {
    fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, path + "/parent", "缺少 parent 字段");
    return false;
  }
  if (yyjson_is_str(parent)) {
    const auto parent_uuid = json_string(parent);
    if (!is_canonical_uuid(parent_uuid)) {
      fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, path + "/parent", "父 UUID 格式错误");
      return false;
    }
    output.parent_uuid = std::string(parent_uuid);
  } else if (!yyjson_is_null(parent)) {
    fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, path + "/parent", "parent 必须是 UUID 或 null");
    return false;
  }
  return parse_transform(yyjson_obj_get(value, "transform"), path + "/transform", output,
                         diagnostic) &&
         parse_components(yyjson_obj_get(value, "components"), path + "/components", output,
                          diagnostic);
}

[[nodiscard]] bool validate_hierarchy(const gneiss::scene_internal::scene_description& scene,
                                      scene_diagnostic& diagnostic) {
  std::unordered_map<std::string_view, std::size_t> indexes;
  indexes.reserve(scene.objects.size());
  for (std::size_t index = 0; index < scene.objects.size(); ++index) {
    if (!indexes.emplace(scene.objects[index].uuid, index).second) {
      fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "/objects/" + std::to_string(index) + "/uuid",
           "对象 UUID 重复");
      return false;
    }
  }
  for (std::size_t index = 0; index < scene.objects.size(); ++index) {
    const auto& parent = scene.objects[index].parent_uuid;
    if (parent && !indexes.contains(*parent)) {
      fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT,
           "/objects/" + std::to_string(index) + "/parent", "父对象不存在");
      return false;
    }
  }
  const auto primary_camera_count = std::ranges::count_if(
      scene.objects, [](const auto& object) { return object.camera && object.camera->is_primary; });
  if (primary_camera_count > 1) {
    fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "/objects", "场景最多只能包含一个主相机");
    return false;
  }
  std::vector<std::uint8_t> colors(scene.objects.size());
  for (std::size_t start = 0; start < scene.objects.size(); ++start) {
    std::size_t current = start;
    while (colors[current] == 0U) {
      colors[current] = 1U;
      const auto& parent = scene.objects[current].parent_uuid;
      if (!parent) {
        break;
      }
      current = indexes.at(*parent);
    }
    if (colors[current] == 1U && scene.objects[current].parent_uuid) {
      fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT,
           "/objects/" + std::to_string(current) + "/parent", "对象层级形成循环");
      return false;
    }
    current = start;
    while (colors[current] == 1U) {
      colors[current] = 2U;
      const auto& parent = scene.objects[current].parent_uuid;
      if (!parent) {
        break;
      }
      current = indexes.at(*parent);
    }
  }
  return true;
}

} // namespace

namespace gneiss::scene_internal {

// NOLINTNEXTLINE(readability-function-cognitive-complexity): 顺序校验保持 Schema 错误优先级明确。
gneiss_result parse_scene_description(std::string_view json, scene_description& out_scene,
                                      scene_diagnostic& out_diagnostic) noexcept {
  out_scene = {};
  out_diagnostic = {};
  try {
    if (json.empty()) {
      fail(out_diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "", "场景文档为空");
      return out_diagnostic.result;
    }
    yyjson_read_err error{};
    using document_ptr = std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)>;
    document_ptr document(yyjson_read_opts(const_cast<char*>(json.data()), json.size(),
                                           YYJSON_READ_NOFLAG, nullptr, &error),
                          &yyjson_doc_free);
    if (!document) {
      fail(out_diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "",
           error.msg != nullptr ? error.msg : "JSON 语法错误", error.pos);
      return out_diagnostic.result;
    }
    yyjson_val* root = yyjson_doc_get_root(document.get());
    constexpr std::array fields{std::string_view{"format"}, std::string_view{"version"},
                                std::string_view{"scene_uuid"}, std::string_view{"objects"}};
    if (!yyjson_is_obj(root) || !has_only_fields(root, fields, "", out_diagnostic)) {
      if (out_diagnostic.result == GNEISS_SUCCESS) {
        fail(out_diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "", "场景根必须是对象");
      }
      return out_diagnostic.result;
    }
    yyjson_val* format = yyjson_obj_get(root, "format");
    if (!yyjson_is_str(format) || json_string(format) != scene_format) {
      fail(out_diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "/format", "场景格式标识错误");
      return out_diagnostic.result;
    }
    yyjson_val* version = yyjson_obj_get(root, "version");
    if (!yyjson_is_uint(version) || yyjson_get_uint(version) != schema_version) {
      fail(out_diagnostic,
           yyjson_is_uint(version) && yyjson_get_uint(version) > schema_version
               ? GNEISS_ERROR_UNSUPPORTED
               : GNEISS_ERROR_INVALID_ARGUMENT,
           "/version", "不支持的场景 Schema 版本");
      return out_diagnostic.result;
    }
    scene_description parsed;
    if (!read_required_string(root, "scene_uuid", "", parsed.uuid, out_diagnostic) ||
        !is_canonical_uuid(parsed.uuid)) {
      if (out_diagnostic.result == GNEISS_SUCCESS) {
        fail(out_diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "/scene_uuid", "场景 UUID 格式错误");
      }
      return out_diagnostic.result;
    }
    yyjson_val* objects = yyjson_obj_get(root, "objects");
    if (!yyjson_is_arr(objects)) {
      fail(out_diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "/objects", "objects 必须是数组");
      return out_diagnostic.result;
    }
    parsed.objects.reserve(yyjson_arr_size(objects));
    std::size_t index = 0;
    std::size_t maximum = 0;
    yyjson_val* object = nullptr;
    yyjson_arr_foreach(objects, index, maximum, object) {
      parsed.objects.emplace_back();
      if (!parse_object(object, index, parsed.objects.back(), out_diagnostic)) {
        return out_diagnostic.result;
      }
    }
    if (!validate_hierarchy(parsed, out_diagnostic)) {
      return out_diagnostic.result;
    }
    out_scene = std::move(parsed);
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    fail(out_diagnostic, GNEISS_ERROR_OUT_OF_MEMORY, "", "内存不足");
    return out_diagnostic.result;
  } catch (...) {
    fail(out_diagnostic, GNEISS_ERROR_INTERNAL, "", "场景解析内部错误");
    return out_diagnostic.result;
  }
}

gneiss_result load_scene_description(const asset_internal::virtual_file_system& file_system,
                                     std::string_view uri, scene_description& out_scene,
                                     scene_diagnostic& out_diagnostic) noexcept {
  std::vector<std::byte> bytes;
  const auto result = file_system.read(uri, bytes);
  if (result != GNEISS_SUCCESS) {
    out_scene = {};
    out_diagnostic = {};
    fail(out_diagnostic, result, "", "无法通过 VFS 读取场景");
    return result;
  }
  return parse_scene_description(
      std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()), out_scene,
      out_diagnostic);
}

} // namespace gneiss::scene_internal
