// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "scene/scene_description.h"

#include "asset/asset_uri.h"
#include "asset/virtual_file_system.h"

#include <yyjson.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>
#include <numbers>
#include <numeric>
#include <type_traits>
#include <unordered_map>

namespace {

using gneiss::scene_internal::scene_diagnostic;

constexpr std::uint64_t schema_version = 4;
constexpr std::string_view scene_format = "gneiss.scene";
constexpr char hex_digits[] = "0123456789abcdef";

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
  if (!yyjson_is_obj(value) ||
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
  if (!yyjson_is_obj(value) ||
      !read_float(value, "vertical_field_of_view_radians", path,
                  output.vertical_field_of_view_radians, diagnostic) ||
      !read_float(value, "near_plane", path, output.near_plane, diagnostic) ||
      !read_float(value, "far_plane", path, output.far_plane, diagnostic)) {
    if (diagnostic.result == GNEISS_SUCCESS) {
      fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, path, "camera 必须是对象");
    }
    return false;
  }
  yyjson_val* primary = yyjson_obj_get(value, "is_primary");
  if (!yyjson_is_bool(primary)) {
    fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, std::string(path) + "/is_primary",
         "is_primary 必须是布尔值");
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
  if (!yyjson_is_obj(value) ||
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
  if (!yyjson_is_obj(value)) {
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
  if (!yyjson_is_obj(value) ||
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
  if (yyjson_val* name = yyjson_obj_get(value, "name"); name != nullptr) {
    if (!yyjson_is_str(name)) {
      fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, path + "/name", "name 必须是字符串");
      return false;
    }
    output.name = std::string(json_string(name));
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

[[nodiscard]] int hex_value(char value) noexcept {
  if (value >= '0' && value <= '9') {
    return value - '0';
  }
  if (value >= 'a' && value <= 'f') {
    return value - 'a' + 10;
  }
  return -1;
}

[[nodiscard]] bool parse_type_id(yyjson_val* value, gneiss_type_id& output) noexcept {
  if (!yyjson_is_str(value) || yyjson_get_len(value) != 32U) {
    return false;
  }
  gneiss_type_id parsed{};
  const auto* text = yyjson_get_str(value);
  for (std::size_t index = 0U; index < 16U; ++index) {
    const auto high = hex_value(text[index * 2U]);
    const auto low = hex_value(text[index * 2U + 1U]);
    if (high < 0 || low < 0) {
      return false;
    }
    parsed.bytes[index] = static_cast<std::uint8_t>((high << 4) | low);
  }
  if (std::ranges::all_of(parsed.bytes, [](std::uint8_t byte) { return byte == 0U; })) {
    return false;
  }
  output = parsed;
  return true;
}

template <std::size_t Size>
[[nodiscard]] bool parse_override_float_array(yyjson_val* value,
                                              std::array<float, Size>& output) noexcept {
  if (!yyjson_is_arr(value) || yyjson_arr_size(value) != Size) {
    return false;
  }
  for (std::size_t index = 0U; index < Size; ++index) {
    auto* item = yyjson_arr_get(value, index);
    const auto number = yyjson_get_num(item);
    if (!yyjson_is_num(item) || !std::isfinite(number) ||
        std::abs(number) > std::numeric_limits<float>::max()) {
      return false;
    }
    output[index] = static_cast<float>(number);
  }
  return true;
}

[[nodiscard]] bool parse_override_value(yyjson_val* object,
                                        gneiss::scene_internal::prefab_property_value& output) {
  if (!yyjson_is_obj(object)) {
    return false;
  }
  auto* kind_value = yyjson_obj_get(object, "kind");
  auto* value = yyjson_obj_get(object, "value");
  if (!yyjson_is_str(kind_value)) {
    return false;
  }
  const auto kind = json_string(kind_value);
  gneiss::scene_internal::prefab_property_value parsed;
  if (kind == "bool" && yyjson_is_bool(value)) {
    parsed.payload = yyjson_get_bool(value);
  } else if (kind == "int64" && yyjson_is_sint(value)) {
    parsed.payload = yyjson_get_sint(value);
  } else if (kind == "uint64" && yyjson_is_uint(value)) {
    parsed.payload = yyjson_get_uint(value);
  } else if (kind == "float32" && yyjson_is_num(value) && std::isfinite(yyjson_get_num(value)) &&
             std::abs(yyjson_get_num(value)) <= std::numeric_limits<float>::max()) {
    parsed.payload = static_cast<float>(yyjson_get_num(value));
  } else if (kind == "float64" && yyjson_is_num(value) && std::isfinite(yyjson_get_num(value))) {
    parsed.payload = yyjson_get_num(value);
  } else if (kind == "string" && yyjson_is_str(value)) {
    parsed.payload = std::string(json_string(value));
  } else if (kind == "type_id") {
    gneiss_type_id id{};
    if (!parse_type_id(value, id)) {
      return false;
    }
    std::array<std::uint8_t, 16> bytes{};
    std::ranges::copy(id.bytes, bytes.begin());
    parsed.payload = bytes;
  } else if (kind == "vec3") {
    std::array<float, 3> numbers{};
    if (!parse_override_float_array(value, numbers)) {
      return false;
    }
    parsed.payload = numbers;
  } else if (kind == "quaternion") {
    std::array<float, 4> numbers{};
    if (!parse_override_float_array(value, numbers)) {
      return false;
    }
    parsed.payload = numbers;
  } else {
    return false;
  }
  output = std::move(parsed);
  return true;
}

[[nodiscard]] bool parse_prefab_override(yyjson_val* value, std::size_t instance_index,
                                         std::size_t override_index, std::string_view instance_uuid,
                                         gneiss::scene_internal::prefab_property_override& output,
                                         scene_diagnostic& diagnostic) {
  const auto path = "/prefab_instances/" + std::to_string(instance_index) + "/overrides/" +
                    std::to_string(override_index);
  if (!yyjson_is_obj(value)) {
    fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, path, "覆盖必须是对象");
    return false;
  }
  std::string source_node_uuid;
  if (!read_required_string(value, "source_node_uuid", path, source_node_uuid, diagnostic) ||
      !is_canonical_uuid(source_node_uuid)) {
    if (diagnostic.result == GNEISS_SUCCESS) {
      fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, path + "/source_node_uuid",
           "源节点 UUID 必须是小写规范形式");
    }
    return false;
  }
  gneiss_type_id type_id{};
  if (!parse_type_id(yyjson_obj_get(value, "type_id"), type_id)) {
    fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, path + "/type_id",
         "Type ID 必须是非零的 32 位小写十六进制");
    return false;
  }
  auto* field = yyjson_obj_get(value, "field_id");
  if (!yyjson_is_uint(field) || yyjson_get_uint(field) == 0U ||
      yyjson_get_uint(field) > std::numeric_limits<gneiss_field_id>::max()) {
    fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, path + "/field_id",
         "Field ID 必须是非零 uint32");
    return false;
  }
  gneiss::scene_internal::prefab_property_value property_value;
  if (!parse_override_value(yyjson_obj_get(value, "value"), property_value)) {
    fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, path + "/value", "覆盖属性值无效");
    return false;
  }
  output = {.key = {.node = {.instance_uuid = std::string(instance_uuid),
                             .source_node_uuid = std::move(source_node_uuid)},
                    .type_id = type_id,
                    .field_id = static_cast<gneiss_field_id>(yyjson_get_uint(field))},
            .value = std::move(property_value)};
  return true;
}

[[nodiscard]] bool
parse_prefab_instance(yyjson_val* value, std::size_t index,
                      gneiss::scene_internal::prefab_instance_description& output,
                      scene_diagnostic& diagnostic) {
  const std::string path = "/prefab_instances/" + std::to_string(index);
  if (!yyjson_is_obj(value) ||
      !read_required_string(value, "instance_uuid", path, output.instance_uuid, diagnostic) ||
      !is_canonical_uuid(output.instance_uuid)) {
    if (diagnostic.result == GNEISS_SUCCESS) {
      fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, path + "/instance_uuid",
           "实例 UUID 必须是小写规范形式");
    }
    return false;
  }
  if (!read_required_string(value, "prefab", path, output.prefab_uri, diagnostic) ||
      gneiss::asset_internal::validate_uri(output.prefab_uri) != GNEISS_SUCCESS) {
    if (diagnostic.result == GNEISS_SUCCESS) {
      fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, path + "/prefab", "Prefab URI 无效");
    }
    return false;
  }
  if (yyjson_val* name = yyjson_obj_get(value, "name"); name != nullptr) {
    if (!yyjson_is_str(name)) {
      fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, path + "/name", "name 必须是字符串");
      return false;
    }
    output.name = std::string(json_string(name));
  }
  yyjson_val* parent = yyjson_obj_get(value, "parent");
  if (yyjson_is_str(parent)) {
    const auto parent_uuid = json_string(parent);
    if (!is_canonical_uuid(parent_uuid)) {
      fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, path + "/parent", "父 UUID 格式错误");
      return false;
    }
    output.parent_uuid = std::string(parent_uuid);
  } else if (!yyjson_is_null(parent)) {
    fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, path + "/parent",
         "parent 必须是普通对象 UUID 或 null");
    return false;
  }
  gneiss::scene_internal::object_description transform;
  if (!parse_transform(yyjson_obj_get(value, "transform"), path + "/transform", transform,
                       diagnostic)) {
    return false;
  }
  output.translation = transform.translation;
  output.rotation = transform.rotation;
  output.scale = transform.scale;
  auto* overrides = yyjson_obj_get(value, "overrides");
  if (!yyjson_is_arr(overrides)) {
    fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, path + "/overrides", "overrides 必须是数组");
    return false;
  }
  output.overrides.reserve(yyjson_arr_size(overrides));
  std::size_t override_index = 0U;
  std::size_t override_maximum = 0U;
  yyjson_val* override_value = nullptr;
  yyjson_arr_foreach(overrides, override_index, override_maximum, override_value) {
    output.overrides.emplace_back();
    if (!parse_prefab_override(override_value, index, override_index, output.instance_uuid,
                               output.overrides.back(), diagnostic)) {
      return false;
    }
  }
  std::ranges::sort(output.overrides, [](const auto& left, const auto& right) {
    return gneiss::scene_internal::prefab_property_override_key_less(left.key, right.key);
  });
  if (std::ranges::adjacent_find(output.overrides, [](const auto& left, const auto& right) {
        return left.key == right.key;
      }) != output.overrides.end()) {
    fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, path + "/overrides", "覆盖作者键重复");
    return false;
  }
  return true;
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
  for (std::size_t index = 0; index < scene.prefab_instances.size(); ++index) {
    const auto& instance = scene.prefab_instances[index];
    if (!indexes.emplace(instance.instance_uuid, scene.objects.size() + index).second) {
      fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT,
           "/prefab_instances/" + std::to_string(index) + "/instance_uuid",
           "Prefab 实例 UUID 与场景作者身份重复");
      return false;
    }
  }
  for (std::size_t index = 0; index < scene.objects.size(); ++index) {
    const auto& parent = scene.objects[index].parent_uuid;
    if (parent) {
      const auto found = indexes.find(*parent);
      if (found == indexes.end() || found->second >= scene.objects.size()) {
        fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT,
             "/objects/" + std::to_string(index) + "/parent", "父级必须是普通场景对象");
        return false;
      }
    }
  }
  for (std::size_t index = 0; index < scene.prefab_instances.size(); ++index) {
    const auto& parent = scene.prefab_instances[index].parent_uuid;
    if (parent) {
      const auto found = indexes.find(*parent);
      if (found == indexes.end() || found->second >= scene.objects.size()) {
        fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT,
             "/prefab_instances/" + std::to_string(index) + "/parent",
             "Prefab 实例父级必须是普通场景对象");
        return false;
      }
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

template <std::size_t Size>
[[nodiscard]] yyjson_mut_val* make_float_array(yyjson_mut_doc* document,
                                               const std::array<float, Size>& values) {
  yyjson_mut_val* array = yyjson_mut_arr(document);
  if (array == nullptr) {
    return nullptr;
  }
  for (const float value : values) {
    if (!yyjson_mut_arr_add_real(document, array, value)) {
      return nullptr;
    }
  }
  return array;
}

[[nodiscard]] bool put_value(yyjson_mut_doc* document, yyjson_mut_val* object, const char* name,
                             yyjson_mut_val* value) {
  return object != nullptr && value != nullptr &&
         yyjson_mut_obj_put(object, yyjson_mut_strcpy(document, name), value);
}

[[nodiscard]] yyjson_mut_val*
make_author_object(yyjson_mut_doc* document,
                   const gneiss::scene_internal::object_description& source) {
  auto* object = yyjson_mut_obj(document);
  auto* transform = yyjson_mut_obj(document);
  auto* components = yyjson_mut_obj(document);
  if (object == nullptr || transform == nullptr || components == nullptr ||
      !yyjson_mut_obj_add_strncpy(document, object, "uuid", source.uuid.data(),
                                  source.uuid.size()) ||
      (!source.name.empty() &&
       !yyjson_mut_obj_add_strncpy(document, object, "name", source.name.data(),
                                   source.name.size())) ||
      !put_value(document, object, "parent",
                 source.parent_uuid ? yyjson_mut_strncpy(document, source.parent_uuid->data(),
                                                         source.parent_uuid->size())
                                    : yyjson_mut_null(document)) ||
      !put_value(document, transform, "translation",
                 make_float_array(document, source.translation)) ||
      !put_value(document, transform, "rotation", make_float_array(document, source.rotation)) ||
      !put_value(document, transform, "scale", make_float_array(document, source.scale)) ||
      !put_value(document, object, "transform", transform)) {
    return nullptr;
  }
  if (source.mesh_renderer) {
    auto* renderer = yyjson_mut_obj(document);
    if (renderer == nullptr ||
        !yyjson_mut_obj_add_strncpy(document, renderer, "mesh",
                                    source.mesh_renderer->mesh_uri.data(),
                                    source.mesh_renderer->mesh_uri.size()) ||
        !yyjson_mut_obj_add_strncpy(document, renderer, "material",
                                    source.mesh_renderer->material_uri.data(),
                                    source.mesh_renderer->material_uri.size()) ||
        !put_value(document, components, "mesh_renderer", renderer)) {
      return nullptr;
    }
  }
  if (source.camera) {
    auto* camera = yyjson_mut_obj(document);
    if (camera == nullptr ||
        !yyjson_mut_obj_add_real(document, camera, "vertical_field_of_view_radians",
                                 source.camera->vertical_field_of_view_radians) ||
        !yyjson_mut_obj_add_real(document, camera, "near_plane", source.camera->near_plane) ||
        !yyjson_mut_obj_add_real(document, camera, "far_plane", source.camera->far_plane) ||
        !yyjson_mut_obj_add_bool(document, camera, "is_primary", source.camera->is_primary) ||
        !put_value(document, components, "camera", camera)) {
      return nullptr;
    }
  }
  return put_value(document, object, "components", components) ? object : nullptr;
}

[[nodiscard]] std::string type_id_text(gneiss_type_id id) {
  std::string text(32U, '0');
  for (std::size_t index = 0U; index < 16U; ++index) {
    text[index * 2U] = hex_digits[id.bytes[index] >> 4U];
    text[index * 2U + 1U] = hex_digits[id.bytes[index] & 0x0FU];
  }
  return text;
}

[[nodiscard]] yyjson_mut_val*
make_override_value(yyjson_mut_doc* document,
                    const gneiss::scene_internal::prefab_property_value& source) {
  auto* object = yyjson_mut_obj(document);
  if (object == nullptr) {
    return nullptr;
  }
  const auto added = std::visit(
      [&](const auto& payload) {
        using value_type = std::decay_t<decltype(payload)>;
        if constexpr (std::is_same_v<value_type, std::monostate>) {
          return false;
        } else if constexpr (std::is_same_v<value_type, bool>) {
          return yyjson_mut_obj_add_str(document, object, "kind", "bool") &&
                 yyjson_mut_obj_add_bool(document, object, "value", payload);
        } else if constexpr (std::is_same_v<value_type, std::int64_t>) {
          return yyjson_mut_obj_add_str(document, object, "kind", "int64") &&
                 yyjson_mut_obj_add_sint(document, object, "value", payload);
        } else if constexpr (std::is_same_v<value_type, std::uint64_t>) {
          return yyjson_mut_obj_add_str(document, object, "kind", "uint64") &&
                 yyjson_mut_obj_add_uint(document, object, "value", payload);
        } else if constexpr (std::is_same_v<value_type, float>) {
          return yyjson_mut_obj_add_str(document, object, "kind", "float32") &&
                 yyjson_mut_obj_add_real(document, object, "value", payload);
        } else if constexpr (std::is_same_v<value_type, double>) {
          return yyjson_mut_obj_add_str(document, object, "kind", "float64") &&
                 yyjson_mut_obj_add_real(document, object, "value", payload);
        } else if constexpr (std::is_same_v<value_type, std::string>) {
          return yyjson_mut_obj_add_str(document, object, "kind", "string") &&
                 yyjson_mut_obj_add_strncpy(document, object, "value", payload.data(),
                                            payload.size());
        } else if constexpr (std::is_same_v<value_type, std::array<std::uint8_t, 16>>) {
          gneiss_type_id id{};
          std::ranges::copy(payload, id.bytes);
          const auto text = type_id_text(id);
          return yyjson_mut_obj_add_str(document, object, "kind", "type_id") &&
                 yyjson_mut_obj_add_strncpy(document, object, "value", text.data(), text.size());
        } else if constexpr (std::is_same_v<value_type, std::array<float, 3>>) {
          return yyjson_mut_obj_add_str(document, object, "kind", "vec3") &&
                 put_value(document, object, "value", make_float_array(document, payload));
        } else {
          return yyjson_mut_obj_add_str(document, object, "kind", "quaternion") &&
                 put_value(document, object, "value", make_float_array(document, payload));
        }
      },
      source.payload);
  return added ? object : nullptr;
}

[[nodiscard]] yyjson_mut_val* make_prefab_overrides(
    yyjson_mut_doc* document,
    const std::vector<gneiss::scene_internal::prefab_property_override>& overrides) {
  auto* array = yyjson_mut_arr(document);
  if (array == nullptr) {
    return nullptr;
  }
  for (const auto& source : overrides) {
    auto* object = yyjson_mut_obj(document);
    const auto type_text = type_id_text(source.key.type_id);
    auto* value = make_override_value(document, source.value);
    if (object == nullptr || value == nullptr ||
        !yyjson_mut_obj_add_strncpy(document, object, "source_node_uuid",
                                    source.key.node.source_node_uuid.data(),
                                    source.key.node.source_node_uuid.size()) ||
        !yyjson_mut_obj_add_strncpy(document, object, "type_id", type_text.data(),
                                    type_text.size()) ||
        !yyjson_mut_obj_add_uint(document, object, "field_id", source.key.field_id) ||
        !yyjson_mut_obj_add_val(document, object, "value", value) ||
        !yyjson_mut_arr_add_val(array, object)) {
      return nullptr;
    }
  }
  return array;
}

[[nodiscard]] yyjson_mut_val*
make_author_prefab_instance(yyjson_mut_doc* document,
                            const gneiss::scene_internal::prefab_instance_description& source) {
  auto* instance = yyjson_mut_obj(document);
  auto* transform = yyjson_mut_obj(document);
  if (instance == nullptr || transform == nullptr ||
      !yyjson_mut_obj_add_strncpy(document, instance, "instance_uuid", source.instance_uuid.data(),
                                  source.instance_uuid.size()) ||
      !yyjson_mut_obj_add_strncpy(document, instance, "prefab", source.prefab_uri.data(),
                                  source.prefab_uri.size()) ||
      (!source.name.empty() &&
       !yyjson_mut_obj_add_strncpy(document, instance, "name", source.name.data(),
                                   source.name.size())) ||
      !put_value(document, instance, "parent",
                 source.parent_uuid ? yyjson_mut_strncpy(document, source.parent_uuid->data(),
                                                         source.parent_uuid->size())
                                    : yyjson_mut_null(document)) ||
      !put_value(document, transform, "translation",
                 make_float_array(document, source.translation)) ||
      !put_value(document, transform, "rotation", make_float_array(document, source.rotation)) ||
      !put_value(document, transform, "scale", make_float_array(document, source.scale)) ||
      !put_value(document, instance, "transform", transform) ||
      !put_value(document, instance, "overrides",
                 make_prefab_overrides(document, source.overrides))) {
    return nullptr;
  }
  return instance;
}

} // namespace

namespace gneiss::scene_internal {

// NOLINTNEXTLINE(readability-function-cognitive-complexity): 顺序校验保持 Schema 错误优先级明确。
gneiss_result parse_current_scene_description(std::string_view json, scene_description& out_scene,
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
    if (!yyjson_is_obj(root)) {
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
    yyjson_val* prefab_instances = yyjson_obj_get(root, "prefab_instances");
    if (!yyjson_is_arr(prefab_instances)) {
      fail(out_diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "/prefab_instances",
           "prefab_instances 必须是数组");
      return out_diagnostic.result;
    }
    parsed.prefab_instances.reserve(yyjson_arr_size(prefab_instances));
    index = 0;
    maximum = 0;
    yyjson_val* prefab_instance = nullptr;
    yyjson_arr_foreach(prefab_instances, index, maximum, prefab_instance) {
      parsed.prefab_instances.emplace_back();
      if (!parse_prefab_instance(prefab_instance, index, parsed.prefab_instances.back(),
                                 out_diagnostic)) {
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

[[nodiscard]] gneiss_result prepare_scene_json(std::string_view json, std::string& out_json,
                                               scene_diagnostic& diagnostic) noexcept {
  out_json.clear();
  try {
    yyjson_read_err error{};
    using document_ptr = std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)>;
    document_ptr document(yyjson_read_opts(const_cast<char*>(json.data()), json.size(),
                                           YYJSON_READ_NOFLAG, nullptr, &error),
                          &yyjson_doc_free);
    if (!document) {
      fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "",
           error.msg != nullptr ? error.msg : "JSON 语法错误", error.pos);
      return diagnostic.result;
    }
    yyjson_val* root = yyjson_doc_get_root(document.get());
    if (!yyjson_is_obj(root)) {
      fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "", "场景根必须是对象");
      return diagnostic.result;
    }
    yyjson_val* format = yyjson_obj_get(root, "format");
    if (!yyjson_is_str(format) || json_string(format) != scene_format) {
      fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "/format", "场景格式标识错误");
      return diagnostic.result;
    }
    yyjson_val* version = yyjson_obj_get(root, "version");
    if (!yyjson_is_uint(version)) {
      fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "/version", "场景 Schema 版本必须是整数");
      return diagnostic.result;
    }
    const auto source_version = yyjson_get_uint(version);
    if (source_version > schema_version) {
      fail(diagnostic, GNEISS_ERROR_UNSUPPORTED, "/version", "场景 Schema 来自未来版本");
      return diagnostic.result;
    }
    if (source_version != schema_version) {
      fail(diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "/version", "不支持旧版场景 Schema");
      return diagnostic.result;
    }

    using mutable_document_ptr = std::unique_ptr<yyjson_mut_doc, decltype(&yyjson_mut_doc_free)>;
    mutable_document_ptr mutable_document(yyjson_doc_mut_copy(document.get(), nullptr),
                                          &yyjson_mut_doc_free);
    if (!mutable_document) {
      fail(diagnostic, GNEISS_ERROR_OUT_OF_MEMORY, "", "无法复制场景作者文档");
      return diagnostic.result;
    }
    std::size_t output_length = 0;
    using text_ptr = std::unique_ptr<char, decltype(&std::free)>;
    text_ptr output(yyjson_mut_write(mutable_document.get(), YYJSON_WRITE_NOFLAG, &output_length),
                    &std::free);
    if (!output) {
      fail(diagnostic, GNEISS_ERROR_OUT_OF_MEMORY, "", "无法复制场景作者文档");
      return diagnostic.result;
    }
    out_json.assign(output.get(), output_length);
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    fail(diagnostic, GNEISS_ERROR_OUT_OF_MEMORY, "", "内存不足");
    return diagnostic.result;
  } catch (...) {
    fail(diagnostic, GNEISS_ERROR_INTERNAL, "", "场景作者文档复制失败");
    return diagnostic.result;
  }
}

gneiss_result parse_scene_description(std::string_view json, scene_description& out_scene,
                                      scene_diagnostic& out_diagnostic) noexcept {
  out_scene = {};
  out_diagnostic = {};
  std::string current_json;
  auto result = prepare_scene_json(json, current_json, out_diagnostic);
  if (result != GNEISS_SUCCESS) {
    return result;
  }
  scene_description parsed;
  result = parse_current_scene_description(current_json, parsed, out_diagnostic);
  if (result != GNEISS_SUCCESS) {
    return result;
  }
  try {
    parsed.source_schema_version = static_cast<std::uint32_t>(schema_version);
    parsed.author_json = std::move(current_json);
    out_scene = std::move(parsed);
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    out_scene = {};
    fail(out_diagnostic, GNEISS_ERROR_OUT_OF_MEMORY, "", "内存不足");
    return out_diagnostic.result;
  } catch (...) {
    out_scene = {};
    fail(out_diagnostic, GNEISS_ERROR_INTERNAL, "", "场景作者文档复制失败");
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

gneiss_result serialize_scene_description(const scene_description& scene,
                                          std::string& out_json) noexcept {
  if (scene.author_json.empty()) {
    return GNEISS_ERROR_INVALID_STATE;
  }
  try {
    yyjson_read_err error{};
    using document_ptr = std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)>;
    document_ptr document(yyjson_read_opts(const_cast<char*>(scene.author_json.data()),
                                           scene.author_json.size(), YYJSON_READ_NOFLAG, nullptr,
                                           &error),
                          &yyjson_doc_free);
    using mutable_document_ptr = std::unique_ptr<yyjson_mut_doc, decltype(&yyjson_mut_doc_free)>;
    mutable_document_ptr mutable_document(
        document ? yyjson_doc_mut_copy(document.get(), nullptr) : nullptr, &yyjson_mut_doc_free);
    if (!mutable_document) {
      return document ? GNEISS_ERROR_OUT_OF_MEMORY : GNEISS_ERROR_INVALID_STATE;
    }
    yyjson_mut_val* root = yyjson_mut_doc_get_root(mutable_document.get());
    yyjson_mut_val* objects = yyjson_mut_obj_get(root, "objects");
    if (!yyjson_mut_is_arr(objects) || yyjson_mut_arr_size(objects) > scene.objects.size()) {
      return GNEISS_ERROR_INVALID_STATE;
    }
    for (std::size_t index = yyjson_mut_arr_size(objects); index < scene.objects.size(); ++index) {
      auto* object = make_author_object(mutable_document.get(), scene.objects[index]);
      if (object == nullptr || !yyjson_mut_arr_add_val(objects, object)) {
        return GNEISS_ERROR_OUT_OF_MEMORY;
      }
    }
    for (std::size_t index = 0; index < scene.objects.size(); ++index) {
      const auto& source = scene.objects[index];
      yyjson_mut_val* object = yyjson_mut_arr_get(objects, index);
      yyjson_mut_val* transform = yyjson_mut_obj_get(object, "transform");
      if (!put_value(
              mutable_document.get(), object, "uuid",
              yyjson_mut_strncpy(mutable_document.get(), source.uuid.data(), source.uuid.size())) ||
          !put_value(mutable_document.get(), object, "parent",
                     source.parent_uuid
                         ? yyjson_mut_strncpy(mutable_document.get(), source.parent_uuid->data(),
                                              source.parent_uuid->size())
                         : yyjson_mut_null(mutable_document.get())) ||
          !put_value(mutable_document.get(), transform, "translation",
                     make_float_array(mutable_document.get(), source.translation)) ||
          !put_value(mutable_document.get(), transform, "rotation",
                     make_float_array(mutable_document.get(), source.rotation)) ||
          !put_value(mutable_document.get(), transform, "scale",
                     make_float_array(mutable_document.get(), source.scale))) {
        return GNEISS_ERROR_OUT_OF_MEMORY;
      }
      if (source.name.empty()) {
        (void)yyjson_mut_obj_remove_key(object, "name");
      } else if (!put_value(mutable_document.get(), object, "name",
                            yyjson_mut_strncpy(mutable_document.get(), source.name.data(),
                                               source.name.size()))) {
        return GNEISS_ERROR_OUT_OF_MEMORY;
      }
      if (source.camera) {
        yyjson_mut_val* camera =
            yyjson_mut_obj_get(yyjson_mut_obj_get(object, "components"), "camera");
        if (!put_value(mutable_document.get(), camera, "vertical_field_of_view_radians",
                       yyjson_mut_real(mutable_document.get(),
                                       source.camera->vertical_field_of_view_radians)) ||
            !put_value(mutable_document.get(), camera, "near_plane",
                       yyjson_mut_real(mutable_document.get(), source.camera->near_plane)) ||
            !put_value(mutable_document.get(), camera, "far_plane",
                       yyjson_mut_real(mutable_document.get(), source.camera->far_plane)) ||
            !put_value(mutable_document.get(), camera, "is_primary",
                       yyjson_mut_bool(mutable_document.get(), source.camera->is_primary))) {
          return GNEISS_ERROR_OUT_OF_MEMORY;
        }
      } else {
        yyjson_mut_val* components = yyjson_mut_obj_get(object, "components");
        (void)yyjson_mut_obj_remove_key(components, "camera");
      }
      if (source.mesh_renderer) {
        yyjson_mut_val* components = yyjson_mut_obj_get(object, "components");
        yyjson_mut_val* renderer = yyjson_mut_obj_get(components, "mesh_renderer");
        if (renderer == nullptr) {
          renderer = yyjson_mut_obj(mutable_document.get());
          if (!put_value(mutable_document.get(), components, "mesh_renderer", renderer)) {
            return GNEISS_ERROR_OUT_OF_MEMORY;
          }
        }
        if (!put_value(mutable_document.get(), renderer, "mesh",
                       yyjson_mut_strncpy(mutable_document.get(),
                                          source.mesh_renderer->mesh_uri.data(),
                                          source.mesh_renderer->mesh_uri.size())) ||
            !put_value(mutable_document.get(), renderer, "material",
                       yyjson_mut_strncpy(mutable_document.get(),
                                          source.mesh_renderer->material_uri.data(),
                                          source.mesh_renderer->material_uri.size()))) {
          return GNEISS_ERROR_OUT_OF_MEMORY;
        }
      } else {
        yyjson_mut_val* components = yyjson_mut_obj_get(object, "components");
        (void)yyjson_mut_obj_remove_key(components, "mesh_renderer");
      }
    }
    yyjson_mut_val* prefab_instances = yyjson_mut_obj_get(root, "prefab_instances");
    if (!yyjson_mut_is_arr(prefab_instances) ||
        yyjson_mut_arr_size(prefab_instances) > scene.prefab_instances.size()) {
      return GNEISS_ERROR_INVALID_STATE;
    }
    for (std::size_t index = yyjson_mut_arr_size(prefab_instances);
         index < scene.prefab_instances.size(); ++index) {
      auto* instance =
          make_author_prefab_instance(mutable_document.get(), scene.prefab_instances[index]);
      if (instance == nullptr || !yyjson_mut_arr_add_val(prefab_instances, instance)) {
        return GNEISS_ERROR_OUT_OF_MEMORY;
      }
    }
    for (std::size_t index = 0; index < scene.prefab_instances.size(); ++index) {
      const auto& source = scene.prefab_instances[index];
      yyjson_mut_val* instance = yyjson_mut_arr_get(prefab_instances, index);
      yyjson_mut_val* transform = yyjson_mut_obj_get(instance, "transform");
      if (!put_value(mutable_document.get(), instance, "instance_uuid",
                     yyjson_mut_strncpy(mutable_document.get(), source.instance_uuid.data(),
                                        source.instance_uuid.size())) ||
          !put_value(mutable_document.get(), instance, "prefab",
                     yyjson_mut_strncpy(mutable_document.get(), source.prefab_uri.data(),
                                        source.prefab_uri.size())) ||
          !put_value(mutable_document.get(), instance, "parent",
                     source.parent_uuid
                         ? yyjson_mut_strncpy(mutable_document.get(), source.parent_uuid->data(),
                                              source.parent_uuid->size())
                         : yyjson_mut_null(mutable_document.get())) ||
          !put_value(mutable_document.get(), transform, "translation",
                     make_float_array(mutable_document.get(), source.translation)) ||
          !put_value(mutable_document.get(), transform, "rotation",
                     make_float_array(mutable_document.get(), source.rotation)) ||
          !put_value(mutable_document.get(), transform, "scale",
                     make_float_array(mutable_document.get(), source.scale))) {
        return GNEISS_ERROR_OUT_OF_MEMORY;
      }
      if (source.name.empty()) {
        (void)yyjson_mut_obj_remove_key(instance, "name");
      } else if (!put_value(mutable_document.get(), instance, "name",
                            yyjson_mut_strncpy(mutable_document.get(), source.name.data(),
                                               source.name.size()))) {
        return GNEISS_ERROR_OUT_OF_MEMORY;
      }
      if (!put_value(mutable_document.get(), instance, "overrides",
                     make_prefab_overrides(mutable_document.get(), source.overrides))) {
        return GNEISS_ERROR_OUT_OF_MEMORY;
      }
    }
    std::size_t output_length = 0;
    using text_ptr = std::unique_ptr<char, decltype(&std::free)>;
    text_ptr output(yyjson_mut_write(mutable_document.get(), YYJSON_WRITE_NOFLAG, &output_length),
                    &std::free);
    if (!output) {
      return GNEISS_ERROR_OUT_OF_MEMORY;
    }
    out_json.assign(output.get(), output_length);
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

} // namespace gneiss::scene_internal
