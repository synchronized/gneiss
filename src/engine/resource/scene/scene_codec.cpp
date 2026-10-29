// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/resource/scene/scene_codec.hpp"
#include "engine/core/error.hpp"
#include <cstring>

#include "engine/resource/asset_uri.hpp"

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

using gneiss::engine::scene_diagnostic;

constexpr std::uint64_t schema_version = 5;
constexpr std::string_view scene_format = "gneiss.scene";
constexpr char hex_digits[] = "0123456789abcdef";

void fail(scene_diagnostic& diagnostic, gneiss::engine::error result, std::string_view path,
          std::string_view message, std::size_t byte_offset = 0) noexcept {
  diagnostic.error = result;
  diagnostic.byte_offset = byte_offset;
  try {
    diagnostic.path = path;
    diagnostic.message = message;
  } catch (...) {
    diagnostic.error = gneiss::engine::error::out_of_memory;
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
    fail(diagnostic, gneiss::engine::error::invalid_argument, field_path, "字段必须是非空字符串");
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
                                    std::span<float, Size> output, scene_diagnostic& diagnostic) {
  yyjson_val* array = yyjson_obj_get(object, name);
  const std::string field_path = std::string(path) + "/" + name;
  if (!yyjson_is_arr(array) || yyjson_arr_size(array) != Size) {
    fail(diagnostic, gneiss::engine::error::invalid_argument, field_path, "数组长度或类型错误");
    return false;
  }
  for (std::size_t index = 0; index < Size; ++index) {
    yyjson_val* value = yyjson_arr_get(array, index);
    const double number = yyjson_get_num(value);
    if (!yyjson_is_num(value) || !std::isfinite(number) ||
        std::abs(number) > std::numeric_limits<float>::max()) {
      fail(diagnostic, gneiss::engine::error::invalid_argument,
           field_path + "/" + std::to_string(index), "数值必须是有限 float");
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
    fail(diagnostic, gneiss::engine::error::invalid_argument, std::string(path) + "/" + name,
         "数值必须是有限 float");
    return false;
  }
  output = static_cast<float>(number);
  return true;
}

[[nodiscard]] bool parse_transform(yyjson_val* value, std::string_view path,
                                   gneiss::engine::object_description& output,
                                   scene_diagnostic& diagnostic) {
  if (!yyjson_is_obj(value) ||
      !read_float_array(value, "translation", path,
                        gneiss::engine::math::components(output.translation), diagnostic) ||
      !read_float_array(value, "rotation", path, gneiss::engine::math::components(output.rotation),
                        diagnostic) ||
      !read_float_array(value, "scale", path, gneiss::engine::math::components(output.scale),
                        diagnostic)) {
    if (diagnostic.error == gneiss::engine::error::success) {
      fail(diagnostic, gneiss::engine::error::invalid_argument, path, "transform 必须是对象");
    }
    return false;
  }
  const double rotation_length =
      std::sqrt(std::inner_product(gneiss::engine::math::components(output.rotation).begin(),
                                   gneiss::engine::math::components(output.rotation).end(),
                                   gneiss::engine::math::components(output.rotation).begin(), 0.0));
  if (std::abs(rotation_length - 1.0) > 1.0e-4 ||
      std::ranges::any_of(gneiss::engine::math::components(output.scale),
                          [](float value) { return std::abs(value) < 1.0e-6F; })) {
    fail(diagnostic, gneiss::engine::error::invalid_argument, path,
         "旋转必须归一化且缩放分量不能为零");
    return false;
  }
  return true;
}

[[nodiscard]] bool parse_camera(yyjson_val* value, std::string_view path,
                                gneiss::engine::camera_description& output,
                                scene_diagnostic& diagnostic) {
  if (!yyjson_is_obj(value) ||
      !read_float(value, "vertical_field_of_view_radians", path,
                  output.vertical_field_of_view_radians, diagnostic) ||
      !read_float(value, "near_plane", path, output.near_plane, diagnostic) ||
      !read_float(value, "far_plane", path, output.far_plane, diagnostic)) {
    if (diagnostic.error == gneiss::engine::error::success) {
      fail(diagnostic, gneiss::engine::error::invalid_argument, path, "camera 必须是对象");
    }
    return false;
  }
  yyjson_val* primary = yyjson_obj_get(value, "is_primary");
  if (!yyjson_is_bool(primary)) {
    fail(diagnostic, gneiss::engine::error::invalid_argument, std::string(path) + "/is_primary",
         "is_primary 必须是布尔值");
    return false;
  }
  output.is_primary = yyjson_get_bool(primary);
  if (output.vertical_field_of_view_radians <= 0.0F ||
      output.vertical_field_of_view_radians >= std::numbers::pi_v<float> ||
      output.near_plane <= 0.0F || output.far_plane <= output.near_plane) {
    fail(diagnostic, gneiss::engine::error::invalid_argument, path, "相机透视参数范围错误");
    return false;
  }
  return true;
}

[[nodiscard]] bool parse_mesh_renderer(yyjson_val* value, std::string_view path,
                                       gneiss::engine::mesh_renderer_description& output,
                                       scene_diagnostic& diagnostic) {
  if (!yyjson_is_obj(value) ||
      !read_required_string(value, "mesh", path, output.mesh_uri, diagnostic) ||
      !read_required_string(value, "material", path, output.material_uri, diagnostic)) {
    if (diagnostic.error == gneiss::engine::error::success) {
      fail(diagnostic, gneiss::engine::error::invalid_argument, path, "mesh_renderer 必须是对象");
    }
    return false;
  }
  if (gneiss::engine::validate_uri(output.mesh_uri) != gneiss::engine::error::success ||
      gneiss::engine::validate_uri(output.material_uri) != gneiss::engine::error::success) {
    fail(diagnostic, gneiss::engine::error::invalid_argument, path, "资源引用必须是规范 asset URI");
    return false;
  }
  return true;
}

[[nodiscard]] bool parse_components(yyjson_val* value, std::string_view path,
                                    gneiss::engine::object_description& output,
                                    scene_diagnostic& diagnostic) {
  if (!yyjson_is_obj(value)) {
    if (diagnostic.error == gneiss::engine::error::success) {
      fail(diagnostic, gneiss::engine::error::invalid_argument, path, "components 必须是对象");
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
                                gneiss::engine::object_description& output,
                                scene_diagnostic& diagnostic) {
  const std::string path = "/objects/" + std::to_string(index);
  if (!yyjson_is_obj(value) ||
      !read_required_string(value, "uuid", path, output.uuid, diagnostic)) {
    if (diagnostic.error == gneiss::engine::error::success) {
      fail(diagnostic, gneiss::engine::error::invalid_argument, path, "对象描述类型错误");
    }
    return false;
  }
  if (!is_canonical_uuid(output.uuid)) {
    fail(diagnostic, gneiss::engine::error::invalid_argument, path + "/uuid",
         "UUID 必须是小写规范形式");
    return false;
  }
  if (yyjson_val* name = yyjson_obj_get(value, "name"); name != nullptr) {
    if (!yyjson_is_str(name)) {
      fail(diagnostic, gneiss::engine::error::invalid_argument, path + "/name",
           "name 必须是字符串");
      return false;
    }
    output.name = std::string(json_string(name));
  }
  yyjson_val* parent = yyjson_obj_get(value, "parent");
  if (parent == nullptr) {
    fail(diagnostic, gneiss::engine::error::invalid_argument, path + "/parent", "缺少 parent 字段");
    return false;
  }
  if (yyjson_is_str(parent)) {
    const auto parent_uuid = json_string(parent);
    if (!is_canonical_uuid(parent_uuid)) {
      fail(diagnostic, gneiss::engine::error::invalid_argument, path + "/parent",
           "父 UUID 格式错误");
      return false;
    }
    output.parent_uuid = std::string(parent_uuid);
  } else if (!yyjson_is_null(parent)) {
    fail(diagnostic, gneiss::engine::error::invalid_argument, path + "/parent",
         "parent 必须是 UUID 或 null");
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

[[nodiscard]] bool parse_type_id(yyjson_val* value, gneiss::engine::guid& output) noexcept {
  if (!yyjson_is_str(value) || yyjson_get_len(value) != 32U) {
    return false;
  }
  gneiss::engine::guid::byte_array parsed{};
  const auto* text = yyjson_get_str(value);
  for (std::size_t index = 0U; index < 16U; ++index) {
    const auto high = hex_value(text[index * 2U]);
    const auto low = hex_value(text[index * 2U + 1U]);
    if (high < 0 || low < 0) {
      return false;
    }
    parsed[index] = static_cast<std::uint8_t>((high << 4) | low);
  }
  if (std::ranges::all_of(parsed, [](std::uint8_t byte) { return byte == 0U; })) {
    return false;
  }
  output = gneiss::engine::guid::from_bytes(parsed);
  return true;
}

template <class T>
[[nodiscard]] bool parse_override_float_array(yyjson_val* value, T& output) noexcept {
  auto components = gneiss::engine::math::components(output);
  const auto size = components.size();
  if (!yyjson_is_arr(value) || yyjson_arr_size(value) != size) {
    return false;
  }
  for (std::size_t index = 0U; index < size; ++index) {
    auto* item = yyjson_arr_get(value, index);
    const auto number = yyjson_get_num(item);
    if (!yyjson_is_num(item) || !std::isfinite(number) ||
        std::abs(number) > std::numeric_limits<float>::max()) {
      return false;
    }
    components[index] = static_cast<float>(number);
  }
  return true;
}

[[nodiscard]] bool parse_override_value(yyjson_val* object, gneiss::engine::value& output) {
  if (!yyjson_is_obj(object)) {
    return false;
  }
  auto* kind_value = yyjson_obj_get(object, "kind");
  auto* value = yyjson_obj_get(object, "value");
  if (!yyjson_is_str(kind_value)) {
    return false;
  }
  const auto kind = json_string(kind_value);
  gneiss::engine::value parsed;
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
    gneiss::engine::guid id{};
    if (!parse_type_id(value, id)) {
      return false;
    }
    parsed.payload = id;
  } else if (kind == "vec3") {
    gneiss::engine::math::vector3 numbers{};
    if (!parse_override_float_array(value, numbers)) {
      return false;
    }
    parsed.payload = numbers;
  } else if (kind == "quaternion") {
    gneiss::engine::math::quaternion numbers{};
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
                                         gneiss::engine::prefab_property_override& output,
                                         scene_diagnostic& diagnostic) {
  const auto path = "/prefab_instances/" + std::to_string(instance_index) + "/overrides/" +
                    std::to_string(override_index);
  if (!yyjson_is_obj(value)) {
    fail(diagnostic, gneiss::engine::error::invalid_argument, path, "覆盖必须是对象");
    return false;
  }
  std::string source_node_uuid;
  if (!read_required_string(value, "source_node_uuid", path, source_node_uuid, diagnostic) ||
      !is_canonical_uuid(source_node_uuid)) {
    if (diagnostic.error == gneiss::engine::error::success) {
      fail(diagnostic, gneiss::engine::error::invalid_argument, path + "/source_node_uuid",
           "源节点 UUID 必须是小写规范形式");
    }
    return false;
  }
  gneiss::engine::guid type_id{};
  if (!parse_type_id(yyjson_obj_get(value, "type_id"), type_id)) {
    fail(diagnostic, gneiss::engine::error::invalid_argument, path + "/type_id",
         "Type ID 必须是非零的 32 位小写十六进制");
    return false;
  }
  auto* field = yyjson_obj_get(value, "field_id");
  if (!yyjson_is_uint(field) || yyjson_get_uint(field) == 0U ||
      yyjson_get_uint(field) > std::numeric_limits<std::uint32_t>::max()) {
    fail(diagnostic, gneiss::engine::error::invalid_argument, path + "/field_id",
         "Field ID 必须是非零 uint32");
    return false;
  }
  gneiss::engine::value parsed_value;
  if (!parse_override_value(yyjson_obj_get(value, "value"), parsed_value)) {
    fail(diagnostic, gneiss::engine::error::invalid_argument, path + "/value", "覆盖属性值无效");
    return false;
  }
  output = {
      .key =
          {
              .node =
                  {
                      .instance_uuid = std::string(instance_uuid),
                      .source_node_uuid = std::move(source_node_uuid),
                  },
              .type_id = type_id,
              .field_id = gneiss::engine::meta::property_id{static_cast<std::uint32_t>(
                  yyjson_get_uint(field))},
          },
      .value = std::move(parsed_value),
  };
  return true;
}

[[nodiscard]] bool parse_prefab_instance(yyjson_val* value, std::size_t index,
                                         gneiss::engine::prefab_instance_description& output,
                                         scene_diagnostic& diagnostic) {
  const std::string path = "/prefab_instances/" + std::to_string(index);
  if (!yyjson_is_obj(value) ||
      !read_required_string(value, "instance_uuid", path, output.instance_uuid, diagnostic) ||
      !is_canonical_uuid(output.instance_uuid)) {
    if (diagnostic.error == gneiss::engine::error::success) {
      fail(diagnostic, gneiss::engine::error::invalid_argument, path + "/instance_uuid",
           "实例 UUID 必须是小写规范形式");
    }
    return false;
  }
  if (!read_required_string(value, "prefab", path, output.prefab_uri, diagnostic) ||
      gneiss::engine::validate_uri(output.prefab_uri) != gneiss::engine::error::success) {
    if (diagnostic.error == gneiss::engine::error::success) {
      fail(diagnostic, gneiss::engine::error::invalid_argument, path + "/prefab",
           "Prefab URI 无效");
    }
    return false;
  }
  if (yyjson_val* name = yyjson_obj_get(value, "name"); name != nullptr) {
    if (!yyjson_is_str(name)) {
      fail(diagnostic, gneiss::engine::error::invalid_argument, path + "/name",
           "name 必须是字符串");
      return false;
    }
    output.name = std::string(json_string(name));
  }
  yyjson_val* parent = yyjson_obj_get(value, "parent");
  if (yyjson_is_str(parent)) {
    const auto parent_uuid = json_string(parent);
    if (!is_canonical_uuid(parent_uuid)) {
      fail(diagnostic, gneiss::engine::error::invalid_argument, path + "/parent",
           "父 UUID 格式错误");
      return false;
    }
    output.parent_uuid = std::string(parent_uuid);
  } else if (!yyjson_is_null(parent)) {
    fail(diagnostic, gneiss::engine::error::invalid_argument, path + "/parent",
         "parent 必须是普通对象 UUID 或 null");
    return false;
  }
  gneiss::engine::object_description transform;
  if (!parse_transform(yyjson_obj_get(value, "transform"), path + "/transform", transform,
                       diagnostic)) {
    return false;
  }
  output.translation = transform.translation;
  output.rotation = transform.rotation;
  output.scale = transform.scale;
  auto* overrides = yyjson_obj_get(value, "overrides");
  if (!yyjson_is_arr(overrides)) {
    fail(diagnostic, gneiss::engine::error::invalid_argument, path + "/overrides",
         "overrides 必须是数组");
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
    return gneiss::engine::prefab_property_override_key_less(left.key, right.key);
  });
  if (std::ranges::adjacent_find(output.overrides, [](const auto& left, const auto& right) {
        return left.key == right.key;
      }) != output.overrides.end()) {
    fail(diagnostic, gneiss::engine::error::invalid_argument, path + "/overrides",
         "覆盖作者键重复");
    return false;
  }
  return true;
}

[[nodiscard]] bool validate_hierarchy(const gneiss::engine::scene_description& scene,
                                      scene_diagnostic& diagnostic) {
  std::unordered_map<std::string_view, std::size_t> indexes;
  indexes.reserve(scene.objects.size());
  for (std::size_t index = 0; index < scene.objects.size(); ++index) {
    if (!indexes.emplace(scene.objects[index].uuid, index).second) {
      fail(diagnostic, gneiss::engine::error::invalid_argument,
           "/objects/" + std::to_string(index) + "/uuid", "对象 UUID 重复");
      return false;
    }
  }
  for (std::size_t index = 0; index < scene.prefab_instances.size(); ++index) {
    const auto& instance = scene.prefab_instances[index];
    if (!indexes.emplace(instance.instance_uuid, scene.objects.size() + index).second) {
      fail(diagnostic, gneiss::engine::error::invalid_argument,
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
        fail(diagnostic, gneiss::engine::error::invalid_argument,
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
        fail(diagnostic, gneiss::engine::error::invalid_argument,
             "/prefab_instances/" + std::to_string(index) + "/parent",
             "Prefab 实例父级必须是普通场景对象");
        return false;
      }
    }
  }
  const auto primary_camera_count = std::ranges::count_if(
      scene.objects, [](const auto& object) { return object.camera && object.camera->is_primary; });
  if (primary_camera_count > 1) {
    fail(diagnostic, gneiss::engine::error::invalid_argument, "/objects",
         "场景最多只能包含一个主相机");
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
      fail(diagnostic, gneiss::engine::error::invalid_argument,
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

template <class Values>
[[nodiscard]] yyjson_mut_val* make_float_array(yyjson_mut_doc* document,
                                               const Values& source_values) {
  const auto values = gneiss::engine::math::components(source_values);
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

[[nodiscard]] yyjson_mut_val* make_author_object(yyjson_mut_doc* document,
                                                 const gneiss::engine::object_description& source) {
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

[[nodiscard]] std::string type_id_text(gneiss::engine::guid id) {
  std::string text(32U, '0');
  for (std::size_t index = 0U; index < 16U; ++index) {
    text[index * 2U] = hex_digits[id.bytes()[index] >> 4U];
    text[index * 2U + 1U] = hex_digits[id.bytes()[index] & 0x0FU];
  }
  return text;
}

[[nodiscard]] yyjson_mut_val* make_override_value(yyjson_mut_doc* document,
                                                  const gneiss::engine::value& source) {
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
        } else if constexpr (std::is_same_v<value_type, gneiss::engine::guid>) {
          const auto& id = payload;
          const auto text = type_id_text(id);
          return yyjson_mut_obj_add_str(document, object, "kind", "type_id") &&
                 yyjson_mut_obj_add_strncpy(document, object, "value", text.data(), text.size());
        } else if constexpr (std::is_same_v<value_type, gneiss::engine::math::vector3>) {
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

[[nodiscard]] yyjson_mut_val*
make_prefab_overrides(yyjson_mut_doc* document,
                      const std::vector<gneiss::engine::prefab_property_override>& overrides) {
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
        !yyjson_mut_obj_add_uint(document, object, "field_id", source.key.field_id.get()) ||
        !yyjson_mut_obj_add_val(document, object, "value", value) ||
        !yyjson_mut_arr_add_val(array, object)) {
      return nullptr;
    }
  }
  return array;
}

[[nodiscard]] yyjson_mut_val*
make_author_prefab_instance(yyjson_mut_doc* document,
                            const gneiss::engine::prefab_instance_description& source) {
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

namespace gneiss::engine {

// NOLINTNEXTLINE(readability-function-cognitive-complexity): 顺序校验保持 Schema 错误优先级明确。
result<scene_description, scene_diagnostic>
parse_current_scene_description(std::string_view json) noexcept {
  scene_diagnostic out_diagnostic;
  try {
    if (json.empty()) {
      fail(out_diagnostic, engine::error::invalid_argument, "", "场景文档为空");
      return out_diagnostic;
    }
    yyjson_read_err error{};
    using document_ptr = std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)>;
    document_ptr document(yyjson_read_opts(const_cast<char*>(json.data()), json.size(),
                                           YYJSON_READ_NOFLAG, nullptr, &error),
                          &yyjson_doc_free);
    if (!document) {
      fail(out_diagnostic, engine::error::invalid_argument, "",
           error.msg != nullptr ? error.msg : "JSON 语法错误", error.pos);
      return out_diagnostic;
    }
    yyjson_val* root = yyjson_doc_get_root(document.get());
    if (!yyjson_is_obj(root)) {
      if (out_diagnostic.error == engine::error::success) {
        fail(out_diagnostic, engine::error::invalid_argument, "", "场景根必须是对象");
      }
      return out_diagnostic;
    }
    yyjson_val* format = yyjson_obj_get(root, "format");
    if (!yyjson_is_str(format) || json_string(format) != scene_format) {
      fail(out_diagnostic, engine::error::invalid_argument, "/format", "场景格式标识错误");
      return out_diagnostic;
    }
    yyjson_val* version = yyjson_obj_get(root, "version");
    if (!yyjson_is_uint(version) || yyjson_get_uint(version) != schema_version) {
      fail(out_diagnostic,
           yyjson_is_uint(version) && yyjson_get_uint(version) > schema_version
               ? engine::error::unsupported
               : engine::error::invalid_argument,
           "/version", "不支持的场景 Schema 版本");
      return out_diagnostic;
    }
    scene_description parsed;
    if (!read_required_string(root, "scene_uuid", "", parsed.uuid, out_diagnostic) ||
        !is_canonical_uuid(parsed.uuid)) {
      if (out_diagnostic.error == engine::error::success) {
        fail(out_diagnostic, engine::error::invalid_argument, "/scene_uuid", "场景 UUID 格式错误");
      }
      return out_diagnostic;
    }
    yyjson_val* objects = yyjson_obj_get(root, "objects");
    if (!yyjson_is_arr(objects)) {
      fail(out_diagnostic, engine::error::invalid_argument, "/objects", "objects 必须是数组");
      return out_diagnostic;
    }
    parsed.objects.reserve(yyjson_arr_size(objects));
    std::size_t index = 0;
    std::size_t maximum = 0;
    yyjson_val* object = nullptr;
    yyjson_arr_foreach(objects, index, maximum, object) {
      parsed.objects.emplace_back();
      if (!parse_object(object, index, parsed.objects.back(), out_diagnostic)) {
        return out_diagnostic;
      }
    }
    yyjson_val* prefab_instances = yyjson_obj_get(root, "prefab_instances");
    if (!yyjson_is_arr(prefab_instances)) {
      fail(out_diagnostic, engine::error::invalid_argument, "/prefab_instances",
           "prefab_instances 必须是数组");
      return out_diagnostic;
    }
    parsed.prefab_instances.reserve(yyjson_arr_size(prefab_instances));
    index = 0;
    maximum = 0;
    yyjson_val* prefab_instance = nullptr;
    yyjson_arr_foreach(prefab_instances, index, maximum, prefab_instance) {
      parsed.prefab_instances.emplace_back();
      if (!parse_prefab_instance(prefab_instance, index, parsed.prefab_instances.back(),
                                 out_diagnostic)) {
        return out_diagnostic;
      }
    }
    if (!validate_hierarchy(parsed, out_diagnostic)) {
      return out_diagnostic;
    }
    return parsed;
  } catch (const std::bad_alloc&) {
    fail(out_diagnostic, engine::error::out_of_memory, "", "内存不足");
    return out_diagnostic;
  } catch (...) {
    fail(out_diagnostic, engine::error::internal, "", "场景解析内部错误");
    return out_diagnostic;
  }
}

[[nodiscard]] result<std::string, scene_diagnostic>
prepare_scene_json(std::string_view json) noexcept {
  scene_diagnostic diagnostic;
  try {
    yyjson_read_err error{};
    using document_ptr = std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)>;
    document_ptr document(yyjson_read_opts(const_cast<char*>(json.data()), json.size(),
                                           YYJSON_READ_NOFLAG, nullptr, &error),
                          &yyjson_doc_free);
    if (!document) {
      fail(diagnostic, engine::error::invalid_argument, "",
           error.msg != nullptr ? error.msg : "JSON 语法错误", error.pos);
      return diagnostic;
    }
    yyjson_val* root = yyjson_doc_get_root(document.get());
    if (!yyjson_is_obj(root)) {
      fail(diagnostic, engine::error::invalid_argument, "", "场景根必须是对象");
      return diagnostic;
    }
    yyjson_val* format = yyjson_obj_get(root, "format");
    if (!yyjson_is_str(format) || json_string(format) != scene_format) {
      fail(diagnostic, engine::error::invalid_argument, "/format", "场景格式标识错误");
      return diagnostic;
    }
    yyjson_val* version = yyjson_obj_get(root, "version");
    if (!yyjson_is_uint(version)) {
      fail(diagnostic, engine::error::invalid_argument, "/version", "场景 Schema 版本必须是整数");
      return diagnostic;
    }
    const auto source_version = yyjson_get_uint(version);
    if (source_version > schema_version) {
      fail(diagnostic, engine::error::unsupported, "/version", "场景 Schema 来自未来版本");
      return diagnostic;
    }
    if (source_version != schema_version) {
      fail(diagnostic, engine::error::invalid_argument, "/version", "不支持旧版场景 Schema");
      return diagnostic;
    }

    using mutable_document_ptr = std::unique_ptr<yyjson_mut_doc, decltype(&yyjson_mut_doc_free)>;
    mutable_document_ptr mutable_document(yyjson_doc_mut_copy(document.get(), nullptr),
                                          &yyjson_mut_doc_free);
    if (!mutable_document) {
      fail(diagnostic, engine::error::out_of_memory, "", "无法复制场景作者文档");
      return diagnostic;
    }
    std::size_t output_length = 0;
    using text_ptr = std::unique_ptr<char, decltype(&std::free)>;
    text_ptr output(yyjson_mut_write(mutable_document.get(), YYJSON_WRITE_NOFLAG, &output_length),
                    &std::free);
    if (!output) {
      fail(diagnostic, engine::error::out_of_memory, "", "无法复制场景作者文档");
      return diagnostic;
    }
    return std::string(output.get(), output_length);
  } catch (const std::bad_alloc&) {
    fail(diagnostic, engine::error::out_of_memory, "", "内存不足");
    return diagnostic;
  } catch (...) {
    fail(diagnostic, engine::error::internal, "", "场景作者文档复制失败");
    return diagnostic;
  }
}

result<scene_description, scene_diagnostic> parse_scene_description(std::string_view json) noexcept
    try {
  auto current_json = prepare_scene_json(json);
  if (!current_json)
    return std::move(current_json).error();
  auto parsed = parse_current_scene_description(*current_json);
  if (!parsed)
    return std::move(parsed).error();
  parsed->source_schema_version = static_cast<std::uint32_t>(schema_version);
  parsed->author_json = std::move(*current_json);
  return std::move(*parsed);
} catch (const std::bad_alloc&) {
  return scene_diagnostic{.error = error::out_of_memory};
} catch (...) {
  return scene_diagnostic{.error = error::internal};
}

result<std::string> serialize_scene_description(const scene_description& scene) noexcept try {
  std::string out_json;

  if (scene.author_json.empty()) {
    return engine::error::invalid_state;
  }

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
    return document ? engine::error::out_of_memory : engine::error::invalid_state;
  }
  yyjson_mut_val* root = yyjson_mut_doc_get_root(mutable_document.get());
  if (!yyjson_mut_is_arr(yyjson_mut_obj_get(root, "objects")) ||
      !yyjson_mut_is_arr(yyjson_mut_obj_get(root, "prefab_instances")))
    return engine::error::invalid_state;
  // 按稳定身份保留扩展字段，不能按旧数组下标绑定删除或重排后的对象。
  const auto align_members = [&](const char* key, const char* identity, const auto& members,
                                 auto get_identity, auto create) -> yyjson_mut_val* {
    auto* previous = yyjson_mut_obj_get(root, key);
    if (!yyjson_mut_is_arr(previous))
      return nullptr;
    std::unordered_map<std::string_view, yyjson_mut_val*> originals;
    auto iterator = yyjson_mut_arr_iter_with(previous);
    while (auto* entry = yyjson_mut_arr_iter_next(&iterator)) {
      auto* id = yyjson_mut_obj_get(entry, identity);
      if (yyjson_mut_is_str(id))
        originals.emplace(std::string_view(yyjson_mut_get_str(id), yyjson_mut_get_len(id)), entry);
    }
    auto* aligned = yyjson_mut_arr(mutable_document.get());
    if (!aligned)
      return nullptr;
    for (const auto& member : members) {
      const auto found = originals.find(get_identity(member));
      auto* entry = found == originals.end()
                        ? create(mutable_document.get(), member)
                        : yyjson_mut_val_mut_copy(mutable_document.get(), found->second);
      if (!entry || !yyjson_mut_arr_add_val(aligned, entry))
        return nullptr;
    }
    return put_value(mutable_document.get(), root, key, aligned) ? aligned : nullptr;
  };
  auto* objects = align_members(
      "objects", "uuid", scene.objects,
      [](const auto& value) { return std::string_view(value.uuid); }, make_author_object);
  if (!objects)
    return engine::error::out_of_memory;
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
      return engine::error::out_of_memory;
    }
    if (source.name.empty()) {
      (void)yyjson_mut_obj_remove_key(object, "name");
    } else if (!put_value(mutable_document.get(), object, "name",
                          yyjson_mut_strncpy(mutable_document.get(), source.name.data(),
                                             source.name.size()))) {
      return engine::error::out_of_memory;
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
        return engine::error::out_of_memory;
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
          return engine::error::out_of_memory;
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
        return engine::error::out_of_memory;
      }
    } else {
      yyjson_mut_val* components = yyjson_mut_obj_get(object, "components");
      (void)yyjson_mut_obj_remove_key(components, "mesh_renderer");
    }
  }
  auto* prefab_instances = align_members(
      "prefab_instances", "instance_uuid", scene.prefab_instances,
      [](const auto& value) { return std::string_view(value.instance_uuid); },
      make_author_prefab_instance);
  if (!prefab_instances)
    return engine::error::out_of_memory;
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
      return engine::error::out_of_memory;
    }
    if (source.name.empty()) {
      (void)yyjson_mut_obj_remove_key(instance, "name");
    } else if (!put_value(mutable_document.get(), instance, "name",
                          yyjson_mut_strncpy(mutable_document.get(), source.name.data(),
                                             source.name.size()))) {
      return engine::error::out_of_memory;
    }
    if (!put_value(mutable_document.get(), instance, "overrides",
                   make_prefab_overrides(mutable_document.get(), source.overrides))) {
      return engine::error::out_of_memory;
    }
  }
  std::size_t output_length = 0;
  using text_ptr = std::unique_ptr<char, decltype(&std::free)>;
  text_ptr output(yyjson_mut_write(mutable_document.get(), YYJSON_WRITE_NOFLAG, &output_length),
                  &std::free);
  if (!output) {
    return engine::error::out_of_memory;
  }
  out_json.assign(output.get(), output_length);
  return out_json;

} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}

} // namespace gneiss::engine

namespace gneiss::engine {
result<scene_description, meta::serialization_diagnostic>
decode_scene(std::span<const std::byte> bytes) noexcept try {
  if (bytes.size() > scene_description::maximum_source_bytes)
    return meta::serialization_diagnostic{.message = "资源描述超过单文件大小上限",
                                          .error = error::invalid_argument};
  auto decoded =
      parse_scene_description({reinterpret_cast<const char*>(bytes.data()), bytes.size()});
  if (!decoded) {
    auto failure = std::move(decoded).error();
    return meta::serialization_diagnostic{failure.byte_offset, std::move(failure.path),
                                          std::move(failure.message), failure.error};
  }
  return std::move(*decoded);
} catch (const std::bad_alloc&) {
  return meta::serialization_diagnostic{.error = error::out_of_memory};
} catch (...) {
  return meta::serialization_diagnostic{.error = error::internal};
}
result<std::vector<std::byte>, meta::serialization_diagnostic>
encode_scene(const scene_description& object) noexcept try {
  auto text = serialize_scene_description(object);
  if (!text)
    return meta::serialization_diagnostic{.error = text.error()};
  std::vector<std::byte> bytes(text->size());
  std::memcpy(bytes.data(), text->data(), text->size());
  auto checked = decode_scene(bytes);
  if (!checked)
    return std::move(checked).error();
  return bytes;
} catch (const std::bad_alloc&) {
  return meta::serialization_diagnostic{.error = error::out_of_memory};
} catch (...) {
  return meta::serialization_diagnostic{.error = error::internal};
}
}
