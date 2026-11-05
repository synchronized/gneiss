// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/resource/scene/scene_codec.hpp"
#include "engine/core/error.hpp"
#include "engine/core/meta/detail/json_value_codec.hpp"
#include "engine/core/meta/registration.hpp"
#include "engine/resource/scene/detail/description_codec.hpp"
#include "engine/resource/scene/object_hierarchy.hpp"
#include "engine/resource/scene/prefab_description.hpp"
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
  if ((!output.mesh_uri.empty() &&
       gneiss::engine::validate_uri(output.mesh_uri) != gneiss::engine::error::success) ||
      (!output.material_uri.empty() &&
       gneiss::engine::validate_uri(output.material_uri) != gneiss::engine::error::success)) {
    fail(diagnostic, gneiss::engine::error::invalid_argument, path, "资源引用必须是规范 asset URI");
    return false;
  }
  return true;
}

// 组件覆盖还包含业务定位字段；仅将类型/内容信封交给通用反射引用解码。
gneiss::engine::result<gneiss::engine::component_description,
                       gneiss::engine::meta::serialization_diagnostic>
read_component(gneiss::engine::guid type, yyjson_val* content) {
  using namespace gneiss::engine;
  std::unique_ptr<yyjson_mut_doc, decltype(&yyjson_mut_doc_free)> document(
      yyjson_mut_doc_new(nullptr), &yyjson_mut_doc_free);
  if (!document)
    return meta::serialization_diagnostic{.error = error::out_of_memory};
  auto* root = yyjson_mut_obj(document.get());
  auto name = type.string();
  if (!root || !yyjson_mut_obj_add_strcpy(document.get(), root, "$type_guid", name.c_str()) ||
      !yyjson_mut_obj_add_val(document.get(), root, "$content",
                              yyjson_val_mut_copy(document.get(), content)))
    return meta::serialization_diagnostic{.error = error::out_of_memory};
  yyjson_mut_doc_set_root(document.get(), root);
  std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)> input(
      yyjson_mut_doc_imut_copy(document.get(), nullptr), &yyjson_doc_free);
  if (!input)
    return meta::serialization_diagnostic{.error = error::out_of_memory};
  auto registry = meta::default_registry();
  if (!registry)
    return meta::serialization_diagnostic{.error = registry.error()};
  component_description result;
  auto view = meta::mutable_instance::from(*registry, result);
  if (!view)
    return meta::serialization_diagnostic{.error = view.error()};
  auto status = meta::detail::read_instance(yyjson_doc_get_root(input.get()), *registry, *view);
  if (status.error.failed())
    return status;
  return result;
}

[[nodiscard]] bool parse_object(yyjson_val* value, std::size_t index,
                                gneiss::engine::object_description& output,
                                scene_diagnostic& diagnostic) {
  const std::string path = "/objects/" + std::to_string(index);
  auto* components = yyjson_obj_get(value, "components");
  if (output.prefab) {
    if (components && (!yyjson_is_arr(components) || yyjson_arr_size(components) != 0)) {
      fail(diagnostic, gneiss::engine::error::invalid_argument, path + "/components",
           "Prefab 引用与独立组件不能同时定义");
      return false;
    }
    return true;
  }
  if (!components) {
    fail(diagnostic, gneiss::engine::error::invalid_argument, path + "/components", "缺少组件数组");
    return false;
  }
  return true;
}

[[nodiscard]] bool parse_override_value(yyjson_val* object, gneiss::engine::value& output) {
  auto decoded = gneiss::engine::meta::detail::read_tagged_value(object);
  if (!decoded)
    return false;
  output = std::move(*decoded);
  return true;
}

[[nodiscard]] bool parse_prefab_override(yyjson_val* value,
                                         gneiss::engine::prefab_property_override& output,
                                         scene_diagnostic& diagnostic) {
  const std::string path;
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
  if (!read_required_guid(value, "type_guid", path, type_id, diagnostic)) {
    fail(diagnostic, gneiss::engine::error::invalid_argument, path + "/type_guid",
         "类型 GUID 必须是非零的规范小写 UUID");
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
  if (auto* steps = yyjson_obj_get(value, "path")) {
    if (!yyjson_is_arr(steps) || yyjson_arr_size(steps) > 64) {
      fail(diagnostic, gneiss::engine::error::invalid_argument, path + "/path", "覆盖路径无效");
      return false;
    }
    size_t i, count;
    yyjson_val* step;
    yyjson_arr_foreach(steps, i, count, step) {
      auto* property = yyjson_obj_get(step, "property_id");
      auto* index = yyjson_obj_get(step, "index");
      if (!yyjson_is_obj(step) || yyjson_obj_size(step) != 1 ||
          (property && (!yyjson_is_uint(property) || !yyjson_get_uint(property) ||
                        yyjson_get_uint(property) > UINT32_MAX)) ||
          (index && (!yyjson_is_uint(index) || yyjson_get_uint(index) > SIZE_MAX)) ||
          (!property && !index)) {
        fail(diagnostic, gneiss::engine::error::invalid_argument, path + "/path",
             "路径步骤必须是 property_id 或 index");
        return false;
      }
      if (property)
        output.key.path.emplace_back(gneiss::engine::meta::property_id{
            static_cast<std::uint32_t>(yyjson_get_uint(property))});
      else
        output.key.path.emplace_back(static_cast<std::size_t>(yyjson_get_uint(index)));
    }
  }
  return true;
}

[[nodiscard]] bool parse_component_override(yyjson_val* item,
                                            gneiss::engine::prefab_component_override& output,
                                            scene_diagnostic& diagnostic) {
  using namespace gneiss::engine;
  const std::string location;
  auto* object = yyjson_obj_get(item, "source_node_uuid");
  auto* type = yyjson_obj_get(item, "$type_guid");
  auto id = yyjson_is_str(object) ? guid::parse(json_string(object)) : std::nullopt;
  auto tid = yyjson_is_str(type) ? guid::parse(json_string(type)) : std::nullopt;
  if (!id || !*id || !tid || !*tid || !is_canonical_uuid(json_string(object)) ||
      !is_canonical_uuid(json_string(type))) {
    fail(diagnostic, error::invalid_argument, location, "组件覆盖身份无效");
    return false;
  }
  if (auto* content = yyjson_obj_get(item, "$content")) {
    if (!yyjson_is_obj(content)) {
      fail(diagnostic, error::invalid_argument, location, "新增组件内容必须为对象");
      return false;
    }
    auto decoded = read_component(*tid, content);
    if (!decoded) {
      fail(diagnostic, decoded.error().error, location + decoded.error().path, "新增组件解码失败");
      return false;
    }
    output = {.source_node_uuid = *id, .type_guid = *tid, .added = std::move(*decoded)};
  } else {
    output = {.source_node_uuid = *id, .type_guid = *tid, .added = std::nullopt};
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

[[nodiscard]] bool put_value(yyjson_mut_doc* document, yyjson_mut_val* object, const char* name,
                             yyjson_mut_val* value) {
  return object != nullptr && value != nullptr &&
         yyjson_mut_obj_put(object, yyjson_mut_strcpy(document, name), value);
}

[[nodiscard]] gneiss::engine::result<yyjson_mut_val*>
make_component_overrides(yyjson_mut_doc* document,
                         const gneiss::engine::prefab_reference_description& reference) {
  using namespace gneiss::engine;
  auto* array = yyjson_mut_arr(document);
  if (!array)
    return error::out_of_memory;
  std::vector<std::pair<guid, guid>> keys;
  for (const auto& value : reference.component_overrides) {
    auto key = std::pair{value.source_node_uuid, value.type_guid};
    if (!key.first || !key.second || std::ranges::find(keys, key) != keys.end() ||
        std::ranges::any_of(reference.overrides, [&](const auto& v) {
          return v.key.source_node_uuid == key.first && v.key.type_guid == key.second;
        }))
      return error::invalid_argument;
    keys.push_back(key);
    yyjson_mut_val* entry{};
    if (value.added) {
      if (!*value.added || (*value.added)->guid() != value.type_guid)
        return error::invalid_argument;
      auto encoded = detail::write_components(document, {*value.added});
      if (!encoded)
        return encoded.error().error;
      entry = yyjson_mut_arr_remove_first(*encoded);
    } else {
      entry = yyjson_mut_obj(document);
      if (!put_value(document, entry, "$type_guid",
                     yyjson_mut_strcpy(document, value.type_guid.string().c_str())))
        return error::out_of_memory;
    }
    if (!put_value(document, entry, "source_node_uuid",
                   yyjson_mut_strcpy(document, value.source_node_uuid.string().c_str())) ||
        !yyjson_mut_arr_add_val(array, entry))
      return error::out_of_memory;
  }
  return array;
}

// 从当前描述生成已注册字段。
[[nodiscard]] yyjson_mut_val* make_override_value(yyjson_mut_doc* document,
                                                  const gneiss::engine::value& source) {
  auto encoded = gneiss::engine::meta::detail::write_tagged_value(document, source);
  return encoded ? *encoded : nullptr;
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
    const auto type_text = source.key.type_guid.string();
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
    if (!source.key.path.empty()) {
      auto* steps = yyjson_mut_arr(document);
      if (!steps || !yyjson_mut_obj_add_val(document, object, "path", steps))
        return nullptr;
      for (const auto& step : source.key.path) {
        auto* entry = yyjson_mut_obj(document);
        if (!entry || !yyjson_mut_arr_add_val(steps, entry))
          return nullptr;
        if (const auto* property = std::get_if<gneiss::engine::meta::property_id>(&step)) {
          if (!yyjson_mut_obj_add_uint(document, entry, "property_id", property->get()))
            return nullptr;
        } else if (!yyjson_mut_obj_add_uint(document, entry, "index", std::get<std::size_t>(step)))
          return nullptr;
      }
    }
  }
  return array;
}

using gneiss::engine::error;
using gneiss::engine::object_description;
using gneiss::engine::result;
namespace meta = gneiss::engine::meta;
template <class T, auto Parse>
result<T, meta::serialization_diagnostic> decode_override(std::span<const std::byte> bytes) {
  std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)> doc(
      yyjson_read(reinterpret_cast<const char*>(bytes.data()), bytes.size(), 0), &yyjson_doc_free);
  if (!doc)
    return meta::serialization_diagnostic{.error = error::invalid_argument};
  T output;
  scene_diagnostic diagnostic;
  if (!Parse(yyjson_doc_get_root(doc.get()), output, diagnostic))
    return meta::serialization_diagnostic{diagnostic.byte_offset, std::move(diagnostic.path),
                                          std::move(diagnostic.message), diagnostic.error};
  return output;
}
template <class T>
result<std::vector<std::byte>, meta::serialization_diagnostic> encode_override(const T& input) {
  using namespace gneiss::engine;
  std::unique_ptr<yyjson_mut_doc, decltype(&yyjson_mut_doc_free)> doc(yyjson_mut_doc_new(nullptr),
                                                                      &yyjson_mut_doc_free);
  if (!doc)
    return meta::serialization_diagnostic{.error = error::out_of_memory};
  yyjson_mut_val* array{};
  if constexpr (std::is_same_v<T, prefab_property_override>) {
    array = make_prefab_overrides(doc.get(), {input});
  } else {
    prefab_reference_description reference;
    reference.component_overrides.push_back(input);
    auto encoded = make_component_overrides(doc.get(), reference);
    if (!encoded)
      return meta::serialization_diagnostic{.error = encoded.error()};
    array = *encoded;
  }
  if (!array)
    return meta::serialization_diagnostic{.error = error::out_of_memory};
  yyjson_mut_doc_set_root(doc.get(), yyjson_mut_arr_remove_first(array));
  std::size_t length{};
  std::unique_ptr<char, decltype(&std::free)> text(yyjson_mut_write(doc.get(), 0, &length),
                                                   &std::free);
  if (!text)
    return meta::serialization_diagnostic{.error = error::out_of_memory};
  auto bytes = std::as_bytes(std::span{text.get(), length});
  return std::vector<std::byte>(bytes.begin(), bytes.end());
}
meta::serialization_diagnostic
check_prefab_reference(const gneiss::engine::prefab_reference_description& value) {
  using namespace gneiss::engine;
  if (validate_uri(value.uri).failed())
    return {
        .path = "/uri", .message = "Prefab 来源必须包含规范 URI", .error = error::invalid_argument};
  std::set<prefab_property_override_key, decltype(&prefab_property_override_key_less)> keys(
      &prefab_property_override_key_less);
  for (std::size_t i = 0; i < value.overrides.size(); ++i) {
    const auto& entry = value.overrides[i];
    bool valid = entry.key.source_node_uuid && entry.key.type_guid &&
                 entry.key.property_id.is_valid() && entry.key.path.size() <= 64 &&
                 meta::valid_scalar(entry.value);
    for (const auto& step : entry.key.path)
      if (const auto* property = std::get_if<meta::property_id>(&step);
          property && !property->is_valid())
        valid = false;
    if (!valid)
      return {.path = "/overrides/" + std::to_string(i),
              .message = "覆盖作者键或属性值无效",
              .error = error::invalid_argument};
    if (!keys.insert(entry.key).second)
      return {.path = "/overrides", .message = "覆盖作者键重复", .error = error::invalid_argument};
  }
  std::set<std::pair<guid, guid>> components;
  for (std::size_t i = 0; i < value.component_overrides.size(); ++i) {
    const auto& entry = value.component_overrides[i];
    if (!entry.source_node_uuid || !entry.type_guid ||
        !components.emplace(entry.source_node_uuid, entry.type_guid).second ||
        (entry.added && (!*entry.added || (*entry.added)->guid() != entry.type_guid)) ||
        std::ranges::any_of(value.overrides, [&](const auto& v) {
          return v.key.source_node_uuid == entry.source_node_uuid &&
                 v.key.type_guid == entry.type_guid;
        }))
      return {.path = "/component_overrides/" + std::to_string(i),
              .message = "组件覆盖重复或与属性覆盖矛盾",
              .error = error::invalid_argument};
  }
  return {};
}
const std::vector<gneiss::engine::prefab_property_override>&
reference_overrides(const gneiss::engine::prefab_reference_description& value) {
  return value.overrides;
}
void set_reference_overrides(
    gneiss::engine::prefab_reference_description& value,
    const std::vector<gneiss::engine::prefab_property_override>& overrides) {
  value.overrides = overrides;
  std::ranges::sort(value.overrides, [](const auto& a, const auto& b) {
    return gneiss::engine::prefab_property_override_key_less(a.key, b.key);
  });
}
result<object_description, meta::serialization_diagnostic>
decode_object(std::span<const std::byte> bytes) {
  auto object = meta::deserialize_reflected<object_description>(bytes);
  if (!object)
    return std::move(object).error();
  if (!object->uuid)
    return meta::serialization_diagnostic{
        .path = "/uuid", .message = "对象 UUID 不能为空", .error = error::invalid_argument};
  std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)> doc(
      yyjson_read(reinterpret_cast<const char*>(bytes.data()), bytes.size(), 0), &yyjson_doc_free);
  if (!doc)
    return meta::serialization_diagnostic{.error = error::out_of_memory};
  scene_diagnostic diagnostic;
  if (!parse_object(yyjson_doc_get_root(doc.get()), 0, *object, diagnostic)) {
    if (diagnostic.path.starts_with("/objects/0"))
      diagnostic.path.erase(0, 10);
    return meta::serialization_diagnostic{diagnostic.byte_offset, std::move(diagnostic.path),
                                          std::move(diagnostic.message), diagnostic.error};
  }
  return std::move(*object);
}
result<std::vector<std::byte>, meta::serialization_diagnostic>
encode_object(const object_description& source) {
  if (!source.uuid)
    return meta::serialization_diagnostic{
        .path = "/uuid", .message = "对象 UUID 不能为空", .error = error::invalid_argument};
  if (source.prefab && !source.components.empty())
    return meta::serialization_diagnostic{.error = error::invalid_argument};
  auto fields = meta::serialize_reflected(source);
  if (!fields)
    return std::move(fields).error();
  if (!source.prefab)
    return fields;
  std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)> parsed(
      yyjson_read(reinterpret_cast<const char*>(fields->data()), fields->size(), 0),
      &yyjson_doc_free);
  if (!parsed)
    return meta::serialization_diagnostic{.error = error::out_of_memory};
  std::unique_ptr<yyjson_mut_doc, decltype(&yyjson_mut_doc_free)> doc(
      yyjson_doc_mut_copy(parsed.get(), nullptr), &yyjson_mut_doc_free);
  if (!doc)
    return meta::serialization_diagnostic{.error = error::out_of_memory};
  auto* root = yyjson_mut_doc_get_root(doc.get());
  yyjson_mut_obj_remove_key(root, "components");
  std::size_t length{};
  std::unique_ptr<char, decltype(&std::free)> text(yyjson_mut_write(doc.get(), 0, &length),
                                                   &std::free);
  if (!text)
    return meta::serialization_diagnostic{.error = error::out_of_memory};
  const auto bytes = std::as_bytes(std::span{text.get(), length});
  return std::vector<std::byte>(bytes.begin(), bytes.end());
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
    auto decoded =
        [&]() -> result<std::vector<object_description>, meta::serialization_diagnostic> {
      const auto bytes = std::as_bytes(std::span{json.data(), json.size()});
      if (prefab) {
        auto fields = meta::deserialize_reflected<prefab_description>(bytes);
        if (!fields)
          return std::move(fields).error();
        return std::move(fields->objects);
      }
      auto fields = meta::deserialize_reflected<scene_description>(bytes);
      if (!fields)
        return std::move(fields).error();
      return std::move(fields->objects);
    }();
    if (!decoded) {
      auto d = std::move(decoded).error();
      return scene_diagnostic{.error = d.error,
                              .byte_offset = d.byte_offset,
                              .path = std::move(d.path),
                              .message = std::move(d.message)};
    }
    parsed.objects = std::move(*decoded);
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
  return scene;
} catch (const std::bad_alloc&) {
  return scene_diagnostic{.error = error::out_of_memory};
} catch (...) {
  return scene_diagnostic{.error = error::internal};
}

result<std::string> serialize_scene_description(const scene_description& scene) noexcept {
  return detail::serialize_object_document(scene.uuid, std::nullopt, scene.objects);
}

result<std::string>
detail::serialize_object_document(guid uuid, std::optional<guid> root_uuid,
                                  std::span<const object_description> object_list) noexcept try {
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
    if (!object.components.empty())
      return error::invalid_argument;
    auto status = check_prefab_reference(*object.prefab);
    if (status.error.failed())
      return status.error;
  }
  using mutable_document_ptr = std::unique_ptr<yyjson_mut_doc, decltype(&yyjson_mut_doc_free)>;
  mutable_document_ptr mutable_document(yyjson_mut_doc_new(nullptr), &yyjson_mut_doc_free);
  if (!mutable_document)
    return error::out_of_memory;
  auto* root = yyjson_mut_obj(mutable_document.get());
  if (!root)
    return error::out_of_memory;
  yyjson_mut_doc_set_root(mutable_document.get(), root);
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
  auto* objects = yyjson_mut_arr(mutable_document.get());
  if (!put_value(mutable_document.get(), root, "objects", objects))
    return error::out_of_memory;
  for (const auto& source : object_list) {
    auto encoded = encode_object(source);
    if (!encoded)
      return encoded.error().error;
    std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)> parsed(
        yyjson_read(reinterpret_cast<const char*>(encoded->data()), encoded->size(), 0),
        &yyjson_doc_free);
    if (!parsed)
      return error::out_of_memory;
    auto* object = yyjson_val_mut_copy(mutable_document.get(), yyjson_doc_get_root(parsed.get()));
    if (!object || !yyjson_mut_arr_add_val(objects, object))
      return error::out_of_memory;
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
namespace {
const std::vector<component_description>& object_components(const object_description& object) {
  return object.components.values();
}
error set_object_components(object_description& object,
                            const std::vector<component_description>& values) {
  return object.components.replace(values);
}
}
error register_component_descriptions() noexcept {
  auto base_status = meta::type_builder<component_description_base>{}.polymorphic().commit();
  if (base_status.failed())
    return base_status;
  constexpr auto required = meta::property_flags::required;
  auto status =
      meta::type_builder<prefab_property_override>{}
          .serialization<&encode_override<prefab_property_override>,
                         &decode_override<prefab_property_override, parse_prefab_override>>()
          .commit();
  if (status.failed())
    return status;
  status =
      meta::type_builder<prefab_component_override>{}
          .serialization<&encode_override<prefab_component_override>,
                         &decode_override<prefab_component_override, parse_component_override>>()
          .commit();
  if (status.failed())
    return status;
  status = meta::type_builder<prefab_reference_description>{}
               .property<&prefab_reference_description::uri>(meta::property_id{1}, "uri", required)
               .property<&reference_overrides, &set_reference_overrides>(meta::property_id{2},
                                                                         "overrides", required)
               .property<&prefab_reference_description::component_overrides>(meta::property_id{3},
                                                                             "component_overrides")
               .serialization()
               .validate<&check_prefab_reference>()
               .commit();
  if (status.failed())
    return status;
  status =
      meta::type_builder<object_description>{}
          .property<&object_description::uuid>(meta::property_id{1}, "uuid", required)
          .property<&object_description::name>(meta::property_id{2}, "name")
          .property<&object_description::parent_uuid>(meta::property_id{3}, "parent", required)
          .property<&object_description::transform>(meta::property_id{4}, "transform", required)
          .property<&object_components, &set_object_components>(meta::property_id{5}, "components")
          .property<&object_description::prefab>(meta::property_id{6}, "prefab")
          .serialization()
          .serialization<&encode_object, &decode_object>()
          .commit();
  if (status.failed())
    return status;
  status = meta::type_builder<math::camera_settings>{}
               .property<&math::camera_settings::fov>(meta::property_id{1}, "fov", required)
               .property<&math::camera_settings::near>(meta::property_id{2}, "near", required)
               .property<&math::camera_settings::far>(meta::property_id{3}, "far", required)
               .serialization()
               .validate<&check_camera>()
               .commit();
  if (status.failed())
    return status;
  status =
      meta::type_builder<camera_description>{}
          .base<component_description_base>()
          .property<&camera_description::settings>(meta::property_id{1}, "settings", required)
          .property<&camera_description::is_primary>(
              meta::property_id{4}, "is_primary", required | meta::property_flags::editor_read_only)
          .serialization()
          .commit();
  if (status.failed())
    return status;
  return meta::type_builder<mesh_renderer_description>{}
      .base<component_description_base>()
      .property<&mesh_renderer_description::mesh_uri>(meta::property_id{1}, "mesh", required)
      .property<&mesh_renderer_description::material_uri>(meta::property_id{2}, "material",
                                                          required)
      .serialization()
      .validate<&check_mesh_renderer>()
      .commit();
}
}

namespace gneiss::engine::detail {
result<component_descriptions, meta::serialization_diagnostic>
read_components(yyjson_val* input) noexcept try {
  auto registry = meta::default_registry();
  if (!registry)
    return meta::serialization_diagnostic{.error = registry.error()};
  std::vector<component_description> decoded;
  auto view = meta::mutable_instance::from(*registry, decoded);
  if (!view)
    return meta::serialization_diagnostic{.error = view.error()};
  auto status = meta::detail::read_instance(input, *registry, *view);
  if (status.error.failed())
    return status;
  component_descriptions result;
  auto replaced = result.replace(decoded);
  if (replaced.failed())
    return meta::serialization_diagnostic{.message = "组件列表包含空项或重复类型",
                                          .error = replaced};
  return result;
} catch (const std::bad_alloc&) {
  return meta::serialization_diagnostic{.error = error::out_of_memory};
} catch (...) {
  return meta::serialization_diagnostic{.error = error::internal};
}
result<yyjson_mut_val*, meta::serialization_diagnostic>
write_components(yyjson_mut_doc* document, const component_descriptions& components) noexcept try {
  auto registry = meta::default_registry();
  if (!registry)
    return meta::serialization_diagnostic{.error = registry.error()};
  auto view = meta::instance::from(*registry, components.values());
  if (!view)
    return meta::serialization_diagnostic{.error = view.error()};
  return meta::detail::write_instance(document, *registry, *view);
} catch (const std::bad_alloc&) {
  return meta::serialization_diagnostic{.error = error::out_of_memory};
} catch (...) {
  return meta::serialization_diagnostic{.error = error::internal};
}
}
