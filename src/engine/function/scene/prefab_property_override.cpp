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
