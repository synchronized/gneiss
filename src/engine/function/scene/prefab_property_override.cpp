// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/scene/prefab_property_override.hpp"
#include "engine/core/error.hpp"
#include "engine/core/meta/serialization.hpp"
#include "engine/core/utf8.hpp"
#include "engine/function/scene/scene_object.hpp"
#include "engine/function/world/world_reflection.hpp"

#include <algorithm>
#include <cmath>
#include <new>
#include <string_view>
#include <type_traits>

namespace {

using namespace gneiss::engine;

[[nodiscard]] bool valid_value(const gneiss::engine::value& value) noexcept {
  if (value.payload.valueless_by_exception()) {
    return false;
  }
  return std::visit(
      [](const auto& payload) noexcept {
        using value_type = std::decay_t<decltype(payload)>;
        if constexpr (std::is_same_v<value_type, std::monostate>) {
          return false;
        } else if constexpr (std::is_same_v<value_type, float> ||
                             std::is_same_v<value_type, double>) {
          return std::isfinite(payload);
        } else if constexpr (std::is_same_v<value_type, std::string>) {
          return payload.find('\0') == std::string_view::npos && utf8::is_valid(payload);
        } else if constexpr (std::is_same_v<value_type, gneiss::engine::guid>) {
          return static_cast<bool>(payload);
        } else if constexpr (std::is_same_v<value_type, gneiss::engine::math::vector2> ||
                             std::is_same_v<value_type, gneiss::engine::math::vector4> ||
                             std::is_same_v<value_type, gneiss::engine::math::vector3> ||
                             std::is_same_v<value_type, gneiss::engine::math::quaternion>) {
          return std::ranges::all_of(gneiss::engine::math::components(payload),
                                     [](float item) { return std::isfinite(item); });
        } else {
          return true;
        }
      },
      value.payload);
}

void normalize_zero(gneiss::engine::value& value) noexcept {
  std::visit(
      [](auto& payload) noexcept {
        using value_type = std::decay_t<decltype(payload)>;
        if constexpr (std::is_same_v<value_type, float> || std::is_same_v<value_type, double>) {
          if (payload == 0) {
            payload = 0;
          }
        } else if constexpr (std::is_same_v<value_type, gneiss::engine::math::vector2> ||
                             std::is_same_v<value_type, gneiss::engine::math::vector4> ||
                             std::is_same_v<value_type, gneiss::engine::math::vector3> ||
                             std::is_same_v<value_type, gneiss::engine::math::quaternion>) {
          for (auto& item : gneiss::engine::math::components(payload)) {
            if (item == 0.0F) {
              item = 0.0F;
            }
          }
        }
      },
      value.payload);
}

} // namespace

namespace gneiss::engine {

error validate_prefab_property_override(const prefab_property_override& value) noexcept try {
  if (!value.key.source_node_uuid || !value.key.type_guid || !value.key.property_id.is_valid() ||
      !valid_value(value.value)) {
    return error::invalid_argument;
  }
  auto field = meta::describe_property(value.key.type_guid, value.key.property_id);
  if (!field)
    return field.error();
  if (!has_flags(field->capabilities, meta::property_capabilities::writable) ||
      value.key.path.size() > 64)
    return error::invalid_argument;
  auto type = field->type;
  for (const auto& step : value.key.path) {
    if (const auto* property = std::get_if<meta::property_id>(&step)) {
      auto properties = type.properties();
      if (!properties)
        return properties.error();
      auto found = std::ranges::find(*properties, *property, &meta::property_info::id);
      if (found == properties->end())
        return error::not_found;
      if (!has_flags(found->capabilities, meta::property_capabilities::writable))
        return error::invalid_argument;
      type = found->type;
    } else {
      if (type.kind() != meta::type_kind::sequence)
        return error::invalid_argument;
      auto element = type.element_type();
      if (!element)
        return element.error();
      type = *element;
    }
  }
  if (type.scalar_kind() != value.value.kind())
    return error::invalid_argument;
  return error::success;
} catch (...) {
  return error::internal;
}

error set_prefab_property_override(std::vector<prefab_property_override>& overrides,
                                   prefab_property_override candidate,
                                   engine::value source_value) noexcept {
  auto operation = validate_prefab_property_override(candidate);
  if (operation != error::success) {
    return operation;
  }
  if (!valid_value(source_value) || source_value.kind() != candidate.value.kind()) {
    return error::invalid_argument;
  }
  normalize_zero(candidate.value);
  normalize_zero(source_value);
  try {
    const auto found = std::ranges::find(overrides, candidate.key, &prefab_property_override::key);
    if (candidate.value == source_value) {
      if (found != overrides.end()) {
        overrides.erase(found);
      }
      return error::success;
    }
    if (found != overrides.end()) {
      found->value = std::move(candidate.value);
    } else {
      overrides.push_back(std::move(candidate));
      std::ranges::sort(overrides, [](const auto& left, const auto& right) {
        return engine::prefab_property_override_key_less(left.key, right.key);
      });
    }
    return error::success;
  } catch (const std::bad_alloc&) {
    return error::out_of_memory;
  } catch (...) {
    return error::internal;
  }
}

namespace {
engine::error apply_candidate_object(const prefab_property_override& value,
                                     const engine::value& native, object_description& object) {
  auto result = engine::error::success;
  const auto is_type = [&](guid type) { return value.key.type_guid == type; };
  if (is_type(transform_component_type)) {
    auto transform = object.transform;
    result = meta::write_property(value.key.type_guid, value.key.property_id, &transform, native);
    if (result != engine::error::success) {
      return result;
    }
    object.transform = transform;
  } else {
    if (!value.key.path.empty())
      return error::unsupported;
    return edit_component(object.components, value.key.type_guid, value.key.property_id, native);
  }
  return engine::error::success;
}
}

error prepare_prefab_reference(const prefab_description& source,
                               const prefab_reference_description& reference,
                               prefab_description& output, scene_diagnostic* diagnostic) noexcept
    try {
  if (diagnostic)
    *diagnostic = {};
  const auto fail = [&](error status, guid object, guid type, std::string suffix,
                        const char* message) {
    if (diagnostic)
      *diagnostic = {.error = status,
                     .path =
                         "/objects/" + object.string() + "/components/" + type.string() + suffix,
                     .message = message};
    return status;
  };
  auto candidate = source;
  std::vector<std::pair<guid, guid>> keys;
  auto types = component_editor_types();
  if (!types)
    return types.error();
  for (const auto& change : reference.component_overrides) {
    auto key = std::pair{change.source_node_uuid, change.type_guid};
    if (!key.first || !key.second || std::ranges::find(keys, key) != keys.end())
      return fail(error::invalid_argument, key.first, key.second, {}, "组件覆盖键无效或重复");
    if (std::ranges::none_of(*types, [&](const auto& type) { return type.type == key.second; }))
      return fail(error::unsupported, key.first, key.second, {}, "组件类型没有运行适配器");
    keys.push_back(key);
    auto target = std::ranges::find(candidate.objects, key.first, &object_description::uuid);
    if (target == candidate.objects.end())
      return fail(error::not_found, key.first, key.second, {}, "覆盖目标对象已不存在");
    if (std::ranges::any_of(reference.overrides, [&](const auto& v) {
          return v.key.source_node_uuid == key.first &&
                 (v.key.type_guid == key.second || (key.second == camera_description::type_guid &&
                                                    v.key.type_guid == camera_component_type));
        }))
      return fail(error::invalid_argument, key.first, key.second, {}, "结构覆盖与属性覆盖矛盾");
    if (change.added) {
      if (change.added->guid() != key.second)
        return fail(error::invalid_argument, key.first, key.second, {}, "新增描述类型不匹配");
      if (target->components.find(key.second))
        return fail(error::invalid_state, key.first, key.second, {},
                    "来源已存在实例新增的同类型组件");
      target->components.set(*change.added);
    } else
      target->components.erase(key.second);
  }
  // 提前定位每条属性覆盖；成组应用仍在下一步统一校验跨字段约束。
  std::vector<prefab_property_override_key> property_keys;
  for (const auto& entry : reference.overrides) {
    const auto& key = entry.key;
    if (std::ranges::find(property_keys, key) != property_keys.end())
      return fail(error::invalid_argument, key.source_node_uuid, key.type_guid, {},
                  "属性覆盖键重复");
    property_keys.push_back(key);
    std::string path = "/properties/" + std::to_string(key.property_id.get());
    for (const auto& step : key.path)
      path += std::holds_alternative<meta::property_id>(step)
                  ? "/properties/" + std::to_string(std::get<meta::property_id>(step).get())
                  : "/indices/" + std::to_string(std::get<std::size_t>(step));
    auto object =
        std::ranges::find(candidate.objects, key.source_node_uuid, &object_description::uuid);
    auto status = validate_prefab_property_override(entry);
    if (status.failed() || object == candidate.objects.end())
      return fail(status.failed() ? status : error{error::not_found}, key.source_node_uuid,
                  key.type_guid, path, "属性覆盖目标不存在或类型不匹配");
    if (key.type_guid != transform_component_type) {
      auto* component = object->components.find(key.type_guid);
      // 兼容内建相机属性身份，具体编辑由适配器负责。
      if (!component && key.type_guid == camera_component_type)
        component = object->components.find(camera_description::type_guid);
      if (!component)
        return fail(error::not_found, key.source_node_uuid, key.type_guid, path,
                    "属性覆盖对应的来源组件已不存在");
      if (key.type_guid == component->guid()) {
        auto root = component->view();
        auto first = root ? root->property(key.property_id) : result<meta::instance>{root.error()};
        auto leaf =
            first ? meta::resolve_path(*first, key.path) : result<meta::instance>{first.error()};
        if (!leaf)
          return fail(leaf.error(), key.source_node_uuid, key.type_guid, path,
                      "属性覆盖路径已失效");
      }
    }
  }
  auto status = prepare_prefab_description(candidate, reference.overrides, output);
  if (status.failed() && diagnostic)
    *diagnostic = {.error = status, .message = "Prefab 覆盖后的组件校验失败"};
  return status;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}

engine::error prepare_prefab_description(const prefab_description& source,
                                         std::span<const prefab_property_override> overrides,
                                         prefab_description& output) noexcept try {
  auto candidate = source;
  for (const auto& value : overrides) {
    const auto validation = validate_prefab_property_override(value);
    if (validation != engine::error::success) {
      return validation;
    }
    auto found =
        std::ranges::find(candidate.objects, value.key.source_node_uuid, &object_description::uuid);
    if (found == candidate.objects.end()) {
      return engine::error::not_found;
    }
    if (found->uuid == candidate.root_uuid && value.key.type_guid == transform_component_type)
      return error::invalid_argument;
    // 描述类型的全部叶覆盖在同一副本中应用，再检查跨字段约束。
    if (found->components.find(value.key.type_guid))
      continue;
    const auto result = apply_candidate_object(value, value.value, *found);
    if (result != engine::error::success) {
      return result;
    }
  }
  for (auto& object : candidate.objects) {
    for (const auto& component : object.components) {
      const auto matches = [&](const prefab_property_override& entry) {
        return entry.key.source_node_uuid == object.uuid && entry.key.type_guid == component.guid();
      };
      if (!std::ranges::any_of(overrides, matches))
        continue;
      auto changed = modify_component(component, [&](meta::mutable_instance root) -> error {
        for (const auto& entry : overrides) {
          if (!matches(entry))
            continue;
          auto first = root.property(entry.key.property_id);
          if (!first)
            return first.error();
          auto target = meta::resolve_path(*first, entry.key.path);
          if (!target)
            return target.error();
          auto status = target->write_value(entry.value);
          if (status.failed())
            return status;
        }
        return error::success;
      });
      if (!changed)
        return changed.error();
      object.components.set(std::move(*changed));
    }
    auto status = validate_scene_components(object);
    if (status.failed())
      return status;
  }
  output = std::move(candidate);
  return engine::error::success;
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}

} // namespace gneiss::engine
