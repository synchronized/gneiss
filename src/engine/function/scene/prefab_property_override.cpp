// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/scene/prefab_property_override.hpp"
#include "engine/core/error.hpp"
#include "engine/core/meta/serialization.hpp"
#include "engine/function/scene/scene_object.hpp"
#include "engine/function/world/world_reflection.hpp"

#include <algorithm>
#include <cmath>
#include <new>
#include <string_view>
#include <type_traits>

namespace {

using namespace gneiss::engine;

[[nodiscard]] bool valid_utf8(std::string_view text) noexcept {
  std::size_t index = 0U;
  while (index < text.size()) {
    const auto first = static_cast<unsigned char>(text[index]);
    if (first == 0U) {
      return false;
    }
    if (first < 0x80U) {
      ++index;
      continue;
    }
    std::size_t continuation_count = 0U;
    std::uint32_t code_point = 0U;
    if (first >= 0xC2U && first <= 0xDFU) {
      continuation_count = 1U;
      code_point = first & 0x1FU;
    } else if (first >= 0xE0U && first <= 0xEFU) {
      continuation_count = 2U;
      code_point = first & 0x0FU;
    } else if (first >= 0xF0U && first <= 0xF4U) {
      continuation_count = 3U;
      code_point = first & 0x07U;
    } else {
      return false;
    }
    if (index + continuation_count >= text.size()) {
      return false;
    }
    for (std::size_t offset = 1U; offset <= continuation_count; ++offset) {
      const auto next = static_cast<unsigned char>(text[index + offset]);
      if ((next & 0xC0U) != 0x80U) {
        return false;
      }
      code_point = (code_point << 6U) | (next & 0x3FU);
    }
    if ((continuation_count == 2U && code_point < 0x800U) ||
        (continuation_count == 3U && code_point < 0x10000U) || code_point > 0x10FFFFU ||
        (code_point >= 0xD800U && code_point <= 0xDFFFU)) {
      return false;
    }
    index += continuation_count + 1U;
  }
  return true;
}

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
          return valid_utf8(payload);
        } else if constexpr (std::is_same_v<value_type, gneiss::engine::guid>) {
          return static_cast<bool>(payload);
        } else if constexpr (std::is_same_v<value_type, gneiss::engine::math::vector3> ||
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
        } else if constexpr (std::is_same_v<value_type, gneiss::engine::math::vector3> ||
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
  if (field->kind != value.value.kind() ||
      !has_flags(field->capabilities, meta::property_capabilities::writable)) {
    return error::invalid_argument;
  }
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
  } else if (is_type(engine::camera_component_type)) {
    if (!object.components.template get<camera_description>()) {
      return engine::error::not_found;
    }
    auto camera = *object.components.template get<camera_description>();
    camera_properties properties;
    auto& settings = properties.settings;
    settings = camera.settings;
    result = meta::write_property(value.key.type_guid, value.key.property_id, &properties, native);
    if (result != engine::error::success) {
      return result;
    }
    camera.settings = settings;
    object.components.set(camera);
  } else if (is_type(mesh_renderer_description::type_guid)) {
    const auto* source = object.components.get<mesh_renderer_description>();
    if (!source)
      return error::not_found;
    auto renderer = *source;
    result = meta::write_property(value.key.type_guid, value.key.property_id, &renderer, native);
    if (result.failed())
      return result;
    object.components.set(std::move(renderer));
  } else {
    // 当前对象格式只存储这些组件，不能将其他类型解释为同一候选内存布局。
    return engine::error::unsupported;
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
    const auto result = apply_candidate_object(value, value.value, *found);
    if (result != engine::error::success) {
      return result;
    }
  }
  for (const auto& object : candidate.objects) {
    auto status = validate_scene_components(object);
    if (status.failed())
      return status;
    if (const auto* renderer = object.components.get<mesh_renderer_description>()) {
      auto validated = meta::serialize(*renderer);
      if (!validated)
        return validated.error().error;
    }
    if (object.components.template get<camera_description>() &&
        object.components.template get<camera_description>()->settings.near >=
            object.components.template get<camera_description>()->settings.far) {
      return engine::error::invalid_argument;
    }
  }
  output = std::move(candidate);
  return engine::error::success;
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}

} // namespace gneiss::engine
