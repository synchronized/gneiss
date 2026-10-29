// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/resource/scene/scene_codec.hpp"
#include "engine/core/error.hpp"
#include "engine/core/meta/registration.hpp"
#include "engine/resource/scene/detail/description_codec.hpp"
#include "engine/resource/scene/object_hierarchy.hpp"
#include <cstring>
#include <set>

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

[[nodiscard]] bool read_required_guid(yyjson_val* object, const char* name, std::string_view path,
                                      gneiss::engine::guid& output, scene_diagnostic& diagnostic) {
  std::string text;
  if (!read_required_string(object, name, path, text, diagnostic))
    return false;
  const auto parsed = gneiss::engine::guid::parse(text);
  if (!is_canonical_uuid(text) || !parsed || !*parsed) {
    fail(diagnostic, gneiss::engine::error::invalid_argument, std::string(path) + "/" + name,
         "UUID 必须是非零的规范小写形式");
    return false;
  }
  output = *parsed;
  return true;
}

yyjson_mut_val* make_uuid(yyjson_mut_doc* doc, gneiss::engine::guid value) {
  const auto text = value.string();
  return yyjson_mut_strncpy(doc, text.data(), text.size());
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

[[nodiscard]] bool validate_transform(const gneiss::engine::math::transform& output,
                                      std::string_view path, scene_diagnostic& diagnostic) {
  const auto finite = [](const auto& values) {
    return std::ranges::all_of(gneiss::engine::math::components(values),
                               [](float value) { return std::isfinite(value); });
  };
  if (!finite(output.translation) || !finite(output.rotation) || !finite(output.scale)) {
    fail(diagnostic, gneiss::engine::error::invalid_argument, path, "变换分量必须为有限值");
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

[[nodiscard]] bool parse_transform(yyjson_val* value, std::string_view path,
                                   gneiss::engine::math::transform& output,
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
  return validate_transform(output, path, diagnostic);
}

[[nodiscard]] bool validate_camera(const gneiss::engine::math::camera_settings& output,
                                   std::string_view path, scene_diagnostic& diagnostic) {
  if (!std::isfinite(output.fov) || !std::isfinite(output.near) || !std::isfinite(output.far) ||
      output.fov <= 0.0F || output.fov >= std::numbers::pi_v<float> || output.near <= 0.0F ||
      output.far <= output.near) {
    fail(diagnostic, gneiss::engine::error::invalid_argument, path, "相机透视参数范围错误");
    return false;
  }
  return true;
}

[[nodiscard]] bool validate_mesh_renderer(const gneiss::engine::mesh_renderer_description& output,
                                          std::string_view path, scene_diagnostic& diagnostic) {
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
  using namespace gneiss::engine;
  if (!yyjson_is_arr(value)) {
    fail(diagnostic, error::invalid_argument, path, "components 必须是数组");
    return false;
  }
  std::size_t index, count;
  yyjson_val* entry;
  yyjson_arr_foreach(value, index, count, entry) {
    const auto location = std::string(path) + "/" + std::to_string(index);
    auto* type = yyjson_obj_get(entry, "$type_guid");
    auto* content = yyjson_obj_get(entry, "$content");
    auto id = yyjson_is_str(type) ? guid::parse(json_string(type)) : std::nullopt;
    if (!yyjson_is_obj(entry) || !id || !*id || !is_canonical_uuid(json_string(type))) {
      fail(diagnostic, error::invalid_argument, location + "/$type_guid",
           "组件类型必须是规范 GUID");
      return false;
    }
    if (output.components.find(*id)) {
      fail(diagnostic, error::invalid_argument, location + "/$type_guid", "重复组件描述类型");
      return false;
    }
    if (!yyjson_is_obj(content)) {
      fail(diagnostic, error::invalid_argument, location + "/$content", "组件内容必须是对象");
      return false;
    }
    std::size_t length{};
    std::unique_ptr<char, decltype(&std::free)> text(yyjson_val_write(content, 0, &length),
                                                     &std::free);
    if (!text) {
      fail(diagnostic, error::out_of_memory, location, "组件内容分配失败");
      return false;
    }
    auto decoded = component_description::decode(*id, std::as_bytes(std::span{text.get(), length}));
    if (!decoded) {
      fail(diagnostic, decoded.error().error, location + "/$content" + decoded.error().path,
           decoded.error().message.empty() ? "组件类型未注册或内容无法解码"
                                           : decoded.error().message);
      return false;
    }
    output.components.set(std::move(*decoded));
  }
  return true;
}

[[nodiscard]] bool parse_prefab_reference(yyjson_val* value, std::size_t index,
                                          gneiss::engine::prefab_reference_description& output,
                                          scene_diagnostic& diagnostic);

[[nodiscard]] bool parse_object(yyjson_val* value, std::size_t index,
                                gneiss::engine::object_description& output,
                                scene_diagnostic& diagnostic) {
  const std::string path = "/objects/" + std::to_string(index);
  if (!yyjson_is_obj(value) || !read_required_guid(value, "uuid", path, output.uuid, diagnostic)) {
    if (diagnostic.error == gneiss::engine::error::success) {
      fail(diagnostic, gneiss::engine::error::invalid_argument, path, "对象描述类型错误");
    }
    return false;
  }
  if (!output.uuid) {
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
    if (!is_canonical_uuid(parent_uuid) || !*gneiss::engine::guid::parse(parent_uuid)) {
      fail(diagnostic, gneiss::engine::error::invalid_argument, path + "/parent",
           "父 UUID 格式错误");
      return false;
    }
    output.parent_uuid = *gneiss::engine::guid::parse(parent_uuid);
  } else if (!yyjson_is_null(parent)) {
    fail(diagnostic, gneiss::engine::error::invalid_argument, path + "/parent",
         "parent 必须是 UUID 或 null");
    return false;
  }
  if (!parse_transform(yyjson_obj_get(value, "transform"), path + "/transform", output.transform,
                       diagnostic))
    return false;
  auto* prefab = yyjson_obj_get(value, "prefab");
  auto* components = yyjson_obj_get(value, "components");
  if (prefab) {
    if (components && (!yyjson_is_arr(components) || yyjson_arr_size(components) != 0)) {
      fail(diagnostic, gneiss::engine::error::invalid_argument, path + "/components",
           "Prefab 引用与独立组件不能同时定义");
      return false;
    }
    output.prefab.emplace();
    return parse_prefab_reference(prefab, index, *output.prefab, diagnostic);
  }
  return parse_components(components, path + "/components", output, diagnostic);
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
                                         std::size_t override_index,
                                         gneiss::engine::prefab_property_override& output,
                                         scene_diagnostic& diagnostic) {
  const auto path = "/objects/" + std::to_string(instance_index) + "/prefab/overrides/" +
                    std::to_string(override_index);
  if (!yyjson_is_obj(value)) {
    fail(diagnostic, gneiss::engine::error::invalid_argument, path, "覆盖必须是对象");
    return false;
  }
  gneiss::engine::guid source_node_uuid;
  if (!read_required_guid(value, "source_node_uuid", path, source_node_uuid, diagnostic) ||
      !source_node_uuid) {
    if (diagnostic.error == gneiss::engine::error::success) {
      fail(diagnostic, gneiss::engine::error::invalid_argument, path + "/source_node_uuid",
           "源节点 UUID 必须是小写规范形式");
    }
    return false;
  }
  gneiss::engine::guid type_id{};
  if (!parse_type_id(yyjson_obj_get(value, "type_guid"), type_id)) {
    fail(diagnostic, gneiss::engine::error::invalid_argument, path + "/type_guid",
         "Type ID 必须是非零的 32 位小写十六进制");
    return false;
  }
  auto* field = yyjson_obj_get(value, "property_id");
  if (!yyjson_is_uint(field) || yyjson_get_uint(field) == 0U ||
      yyjson_get_uint(field) > std::numeric_limits<std::uint32_t>::max()) {
    fail(diagnostic, gneiss::engine::error::invalid_argument, path + "/property_id",
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
              .source_node_uuid = std::move(source_node_uuid),
              .type_guid = type_id,
              .property_id = gneiss::engine::meta::property_id{static_cast<std::uint32_t>(
                  yyjson_get_uint(field))},
          },
      .value = std::move(parsed_value),
  };
  return true;
}

[[nodiscard]] bool parse_prefab_reference(yyjson_val* value, std::size_t index,
                                          gneiss::engine::prefab_reference_description& output,
                                          scene_diagnostic& diagnostic) {
  const auto path = "/objects/" + std::to_string(index) + "/prefab";
  if (!yyjson_is_obj(value) || !read_required_string(value, "uri", path, output.uri, diagnostic) ||
      gneiss::engine::validate_uri(output.uri) != gneiss::engine::error::success) {
    fail(diagnostic, gneiss::engine::error::invalid_argument, path, "Prefab 来源必须包含规范 URI");
    return false;
  }
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
    if (!parse_prefab_override(override_value, index, override_index, output.overrides.back(),
                               diagnostic)) {
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

[[nodiscard]] bool validate_hierarchy(std::span<const gneiss::engine::object_description> objects,
                                      std::optional<gneiss::engine::guid> root,
                                      scene_diagnostic& diagnostic) {
  auto inspected = gneiss::engine::inspect_object_hierarchy(objects, root);
  if (!inspected) {
    diagnostic = std::move(inspected).error();
    return false;
  }
  const auto primary_camera_count = std::ranges::count_if(objects, [](const auto& object) {
    auto* camera = object.components.template get<gneiss::engine::camera_description>();
    return camera && camera->is_primary;
  });
  if (primary_camera_count > 1) {
    fail(diagnostic, gneiss::engine::error::invalid_argument, "/objects",
         "场景最多只能包含一个主相机");
    return false;
  }
  if (root &&
      std::ranges::any_of(objects, [](const auto& object) { return object.prefab.has_value(); })) {
    fail(diagnostic, gneiss::engine::error::unsupported, "/objects", "暂不支持嵌套 Prefab 引用");
    return false;
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

[[nodiscard]] gneiss::engine::result<yyjson_mut_val*>
make_components(yyjson_mut_doc* document, const gneiss::engine::component_descriptions& values,
                yyjson_mut_val* previous = nullptr) {
  auto* array = yyjson_mut_arr(document);
  if (!array)
    return gneiss::engine::error::out_of_memory;
  for (const auto& value : values) {
    auto encoded = value.encode();
    if (!encoded)
      return encoded.error().error;
    std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)> decoded(
        yyjson_read(reinterpret_cast<const char*>(encoded->data()), encoded->size(), 0),
        &yyjson_doc_free);
    if (!decoded || !yyjson_is_obj(yyjson_doc_get_root(decoded.get())))
      return gneiss::engine::error::invalid_argument;
    const auto type = value.guid().string();
    auto* entry = yyjson_mut_obj(document);
    if (yyjson_mut_is_arr(previous)) {
      auto it = yyjson_mut_arr_iter_with(previous);
      while (auto* old = yyjson_mut_arr_iter_next(&it)) {
        auto* old_type = yyjson_mut_obj_get(old, "$type_guid");
        if (yyjson_mut_is_str(old_type) && type == yyjson_mut_get_str(old_type)) {
          entry = yyjson_mut_val_mut_copy(document, old);
          break;
        }
      }
    }
    // 内容完全由 codec 输出；不能合并旧字段，否则省略的默认值或删除的字段会复活。
    auto* content = yyjson_val_mut_copy(document, yyjson_doc_get_root(decoded.get()));
    if (!put_value(document, entry, "$type_guid", yyjson_mut_strcpy(document, type.c_str())) ||
        !put_value(document, entry, "$content", content) || !yyjson_mut_arr_add_val(array, entry))
      return gneiss::engine::error::out_of_memory;
  }
  return array;
}

[[nodiscard]] yyjson_mut_val* make_author_object(yyjson_mut_doc* document,
                                                 const gneiss::engine::object_description& source) {
  auto* object = yyjson_mut_obj(document);
  auto* transform = yyjson_mut_obj(document);
  auto* components = yyjson_mut_arr(document);
  if (object == nullptr || transform == nullptr || components == nullptr ||
      !yyjson_mut_obj_add_val(document, object, "uuid", make_uuid(document, source.uuid)) ||
      (!source.name.empty() &&
       !yyjson_mut_obj_add_strncpy(document, object, "name", source.name.data(),
                                   source.name.size())) ||
      !put_value(document, object, "parent",
                 source.parent_uuid ? make_uuid(document, *source.parent_uuid)
                                    : yyjson_mut_null(document)) ||
      !put_value(document, transform, "translation",
                 make_float_array(document, source.transform.translation)) ||
      !put_value(document, transform, "rotation",
                 make_float_array(document, source.transform.rotation)) ||
      !put_value(document, transform, "scale",
                 make_float_array(document, source.transform.scale)) ||
      !put_value(document, object, "transform", transform)) {
    return nullptr;
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
    const auto type_text = type_id_text(source.key.type_guid);
    auto* value = make_override_value(document, source.value);
    if (object == nullptr || value == nullptr ||
        !yyjson_mut_obj_add_val(document, object, "source_node_uuid",
                                make_uuid(document, source.key.source_node_uuid)) ||
        !yyjson_mut_obj_add_strncpy(document, object, "type_guid", type_text.data(),
                                    type_text.size()) ||
        !yyjson_mut_obj_add_uint(document, object, "property_id", source.key.property_id.get()) ||
        !yyjson_mut_obj_add_val(document, object, "value", value) ||
        !yyjson_mut_arr_add_val(array, object)) {
      return nullptr;
    }
  }
  return array;
}

} // namespace

namespace gneiss::engine {

// NOLINTNEXTLINE(readability-function-cognitive-complexity): 顺序校验保持 Schema 错误优先级明确。
result<detail::decoded_object_document, scene_diagnostic>
detail::parse_object_document(std::string_view json, bool prefab) noexcept {
  const auto format_name = prefab ? "gneiss.prefab" : scene_format;
  const auto version_number = prefab ? 1U : schema_version;
  const auto* uuid_key = prefab ? "prefab_uuid" : "scene_uuid";
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
    if (!yyjson_is_str(format) || json_string(format) != format_name) {
      fail(out_diagnostic, engine::error::invalid_argument, "/format", "场景格式标识错误");
      return out_diagnostic;
    }
    yyjson_val* version = yyjson_obj_get(root, "version");
    if (!yyjson_is_uint(version) || yyjson_get_uint(version) != version_number) {
      fail(out_diagnostic,
           yyjson_is_uint(version) && yyjson_get_uint(version) > version_number
               ? engine::error::unsupported
               : engine::error::invalid_argument,
           "/version", "不支持的场景 Schema 版本");
      return out_diagnostic;
    }
    decoded_object_document parsed;
    if (!read_required_guid(root, uuid_key, "", parsed.uuid, out_diagnostic) || !parsed.uuid) {
      if (out_diagnostic.error == engine::error::success) {
        fail(out_diagnostic, engine::error::invalid_argument, std::string("/") + uuid_key,
             "资源 UUID 格式错误");
      }
      return out_diagnostic;
    }
    if (prefab) {
      guid root_id;
      if (!read_required_guid(root, "root_uuid", "", root_id, out_diagnostic) || !root_id) {
        if (out_diagnostic.error == engine::error::success)
          fail(out_diagnostic, engine::error::invalid_argument, "/root_uuid", "根 UUID 不能为零");
        return out_diagnostic;
      }
      parsed.root_uuid = root_id;
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
    if (yyjson_obj_get(root, "prefab_instances")) {
      fail(out_diagnostic, engine::error::invalid_argument, "/prefab_instances",
           "独立 Prefab 实例数组已移除，请使用 objects[].prefab");
      return out_diagnostic;
    }
    if (!validate_hierarchy(parsed.objects, parsed.root_uuid, out_diagnostic)) {
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

result<scene_description, scene_diagnostic> parse_scene_description(std::string_view json) noexcept
    try {
  auto parsed = detail::parse_object_document(json, false);
  if (!parsed)
    return std::move(parsed).error();
  scene_description scene;
  scene.source_schema_version = static_cast<std::uint32_t>(schema_version);
  scene.uuid = parsed->uuid;
  scene.objects = std::move(parsed->objects);
  scene.author_json = json;
  return scene;
} catch (const std::bad_alloc&) {
  return scene_diagnostic{.error = error::out_of_memory};
} catch (...) {
  return scene_diagnostic{.error = error::internal};
}

result<std::string> serialize_scene_description(const scene_description& scene) noexcept {
  return detail::serialize_object_document(scene.uuid, std::nullopt, scene.objects,
                                           scene.author_json);
}

result<std::string>
detail::serialize_object_document(guid uuid, std::optional<guid> root_uuid,
                                  std::span<const object_description> object_list,
                                  std::string_view author_json) noexcept try {
  std::string out_json;
  if (!uuid)
    return engine::error::invalid_argument;
  scene_diagnostic diagnostic;
  if (!validate_hierarchy(object_list, root_uuid, diagnostic))
    return diagnostic.error;
  for (const auto& object : object_list) {
    if (!validate_transform(object.transform, "/objects/transform", diagnostic))
      return diagnostic.error;
    if (!object.prefab)
      continue;
    if (validate_uri(object.prefab->uri).failed() || !object.components.empty())
      return error::invalid_argument;
    std::set<prefab_property_override_key, decltype(&prefab_property_override_key_less)> keys(
        &prefab_property_override_key_less);
    for (const auto& entry : object.prefab->overrides) {
      if (!entry.key.source_node_uuid || !entry.key.type_guid ||
          !entry.key.property_id.is_valid() || !keys.insert(entry.key).second)
        return error::invalid_argument;
      const bool valid = std::visit(
          [](const auto& item) {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, std::monostate>)
              return false;
            else if constexpr (std::is_same_v<T, guid>)
              return !item.is_nil();
            else if constexpr (std::is_floating_point_v<T>)
              return std::isfinite(item);
            else if constexpr (std::is_same_v<T, math::vector3> ||
                               std::is_same_v<T, math::quaternion>)
              return std::ranges::all_of(math::components(item),
                                         [](float value) { return std::isfinite(value); });
            else
              return true;
          },
          entry.value.payload);
      if (!valid)
        return error::invalid_argument;
    }
  }
  if (author_json.empty()) {
    return engine::error::invalid_state;
  }

  yyjson_read_err error{};
  using document_ptr = std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)>;
  document_ptr document(yyjson_read_opts(const_cast<char*>(author_json.data()), author_json.size(),
                                         YYJSON_READ_NOFLAG, nullptr, &error),
                        &yyjson_doc_free);
  using mutable_document_ptr = std::unique_ptr<yyjson_mut_doc, decltype(&yyjson_mut_doc_free)>;
  mutable_document_ptr mutable_document(
      document ? yyjson_doc_mut_copy(document.get(), nullptr) : nullptr, &yyjson_mut_doc_free);
  if (!mutable_document) {
    return document ? engine::error::out_of_memory : engine::error::invalid_state;
  }
  yyjson_mut_val* root = yyjson_mut_doc_get_root(mutable_document.get());
  if (!yyjson_mut_is_arr(yyjson_mut_obj_get(root, "objects")))
    return engine::error::invalid_state;
  if (!put_value(mutable_document.get(), root, root_uuid ? "prefab_uuid" : "scene_uuid",
                 make_uuid(mutable_document.get(), uuid)))
    return engine::error::out_of_memory;
  if (!put_value(
          mutable_document.get(), root, "format",
          yyjson_mut_str(mutable_document.get(), root_uuid ? "gneiss.prefab" : "gneiss.scene")) ||
      !put_value(mutable_document.get(), root, "version",
                 yyjson_mut_uint(mutable_document.get(), root_uuid ? 1U : schema_version)) ||
      (root_uuid && !put_value(mutable_document.get(), root, "root_uuid",
                               make_uuid(mutable_document.get(), *root_uuid))))
    return engine::error::out_of_memory;
  (void)yyjson_mut_obj_remove_key(root, "prefab_instances");
  // 按稳定身份保留扩展字段，不能按旧数组下标绑定删除或重排后的对象。
  const auto align_members = [&](const char* key, const char* identity, const auto& members,
                                 auto get_identity, auto create) -> yyjson_mut_val* {
    auto* previous = yyjson_mut_obj_get(root, key);
    if (!yyjson_mut_is_arr(previous))
      return nullptr;
    std::unordered_map<guid, yyjson_mut_val*, guid_hash> originals;
    auto iterator = yyjson_mut_arr_iter_with(previous);
    while (auto* entry = yyjson_mut_arr_iter_next(&iterator)) {
      auto* id = yyjson_mut_obj_get(entry, identity);
      if (yyjson_mut_is_str(id))
        if (auto parsed = guid::parse({yyjson_mut_get_str(id), yyjson_mut_get_len(id)}))
          originals.emplace(*parsed, entry);
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
      "objects", "uuid", object_list, [](const auto& value) { return value.uuid; },
      make_author_object);
  if (!objects)
    return engine::error::out_of_memory;
  for (std::size_t index = 0; index < object_list.size(); ++index) {
    const auto& source = object_list[index];
    yyjson_mut_val* object = yyjson_mut_arr_get(objects, index);
    yyjson_mut_val* transform = yyjson_mut_obj_get(object, "transform");
    if (!put_value(mutable_document.get(), object, "uuid",
                   make_uuid(mutable_document.get(), source.uuid)) ||
        !put_value(mutable_document.get(), object, "parent",
                   source.parent_uuid ? make_uuid(mutable_document.get(), *source.parent_uuid)
                                      : yyjson_mut_null(mutable_document.get())) ||
        !put_value(mutable_document.get(), transform, "translation",
                   make_float_array(mutable_document.get(), source.transform.translation)) ||
        !put_value(mutable_document.get(), transform, "rotation",
                   make_float_array(mutable_document.get(), source.transform.rotation)) ||
        !put_value(mutable_document.get(), transform, "scale",
                   make_float_array(mutable_document.get(), source.transform.scale))) {
      return engine::error::out_of_memory;
    }
    if (source.name.empty()) {
      (void)yyjson_mut_obj_remove_key(object, "name");
    } else if (!put_value(mutable_document.get(), object, "name",
                          yyjson_mut_strncpy(mutable_document.get(), source.name.data(),
                                             source.name.size()))) {
      return engine::error::out_of_memory;
    }
    if (source.prefab) {
      if (!source.components.empty())
        return engine::error::invalid_argument;
      auto* prefab = yyjson_mut_obj_get(object, "prefab");
      if (!prefab)
        prefab = yyjson_mut_obj(mutable_document.get());
      if (!yyjson_mut_is_obj(prefab) ||
          !put_value(mutable_document.get(), prefab, "uri",
                     yyjson_mut_strncpy(mutable_document.get(), source.prefab->uri.data(),
                                        source.prefab->uri.size())) ||
          !put_value(mutable_document.get(), prefab, "overrides",
                     make_prefab_overrides(mutable_document.get(), source.prefab->overrides)) ||
          !put_value(mutable_document.get(), object, "prefab", prefab))
        return engine::error::out_of_memory;
      (void)yyjson_mut_obj_remove_key(object, "components");
      continue;
    }
    (void)yyjson_mut_obj_remove_key(object, "prefab");
    auto* previous = yyjson_mut_obj_get(object, "components");
    auto components = make_components(mutable_document.get(), source.components, previous);
    if (!components)
      return components.error();
    if (!put_value(mutable_document.get(), object, "components", *components))
      return engine::error::out_of_memory;
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
  if (text->size() > scene_description::maximum_source_bytes)
    return meta::serialization_diagnostic{.message = "资源描述超过单文件大小上限",
                                          .error = error::invalid_argument};
  std::vector<std::byte> bytes(text->size());
  std::memcpy(bytes.data(), text->data(), text->size());
  return bytes;
} catch (const std::bad_alloc&) {
  return meta::serialization_diagnostic{.error = error::out_of_memory};
} catch (...) {
  return meta::serialization_diagnostic{.error = error::internal};
}
}

namespace gneiss::engine {
namespace {
meta::serialization_diagnostic check_camera(const math::camera_settings& value) {
  scene_diagnostic diagnostic;
  (void)validate_camera(value, "", diagnostic);
  return {diagnostic.byte_offset, std::move(diagnostic.path), std::move(diagnostic.message),
          diagnostic.error};
}
meta::serialization_diagnostic check_mesh_renderer(const mesh_renderer_description& value) {
  scene_diagnostic diagnostic;
  (void)validate_mesh_renderer(value, "", diagnostic);
  return {diagnostic.byte_offset, std::move(diagnostic.path), std::move(diagnostic.message),
          diagnostic.error};
}
}
error register_component_descriptions(const meta::registry_ref& registry) noexcept {
  constexpr auto required = meta::property_flags::required;
  auto status = meta::type_builder<math::camera_settings>{registry}
                    .property<&math::camera_settings::fov>(meta::property_id{1}, "fov", required)
                    .property<&math::camera_settings::near>(meta::property_id{2}, "near", required)
                    .property<&math::camera_settings::far>(meta::property_id{3}, "far", required)
                    .serialization()
                    .validate<&check_camera>()
                    .commit();
  if (status.failed())
    return status;
  status =
      meta::type_builder<camera_description>{registry}
          .property<&camera_description::settings>(meta::property_id{1}, "settings", required)
          .property<&camera_description::is_primary>(meta::property_id{4}, "is_primary", required)
          .serialization()
          .commit();
  if (status.failed())
    return status;
  return meta::type_builder<mesh_renderer_description>{registry}
      .property<&mesh_renderer_description::mesh_uri>(meta::property_id{1}, "mesh", required)
      .property<&mesh_renderer_description::material_uri>(meta::property_id{2}, "material",
                                                          required)
      .serialization()
      .validate<&check_mesh_renderer>()
      .commit();
}
}
