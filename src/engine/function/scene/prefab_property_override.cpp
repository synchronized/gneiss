// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/scene/prefab_property_override.hpp"
#include "engine/core/error.hpp"
#include "engine/core/serialization/json.hpp"
#include "engine/core/serialization/value.hpp"
#include "engine/core/utf8.hpp"
#include "engine/function/scene/scene_object.hpp"
#include "engine/function/world/component_identity.hpp"

#include <algorithm>
#include <cmath>
#include <new>
#include <string_view>
#include <type_traits>

namespace {

using namespace gneiss::engine;

[[nodiscard]] bool valid_value(const gneiss::engine::value& value) noexcept {
  if (value.kind() == value_kind::object || value.payload.valueless_by_exception()) {
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
                             std::is_same_v<value_type, gneiss::engine::math::quaternion> ||
                             std::is_same_v<value_type, math::matrix3> ||
                             std::is_same_v<value_type, math::matrix4>) {
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
                             std::is_same_v<value_type, gneiss::engine::math::quaternion> ||
                             std::is_same_v<value_type, math::matrix3> ||
                             std::is_same_v<value_type, math::matrix4>) {
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

error apply_prefab_override_operation(meta::mutable_instance target,
                                      const value_operation& operation) noexcept {
  auto captured = serialization::capture_value(target.as_const());
  if (!captured)
    return captured.error().error;
  const std::array edits{value_edit{{}, operation}};
  auto updated = apply_value_edits(*captured, edits);
  return updated ? serialization::apply_value(target, *updated).error : updated.error().error;
}

error validate_prefab_property_override(const prefab_property_override& value) noexcept try {
  if (!value.key.target.source_node_uuid || !value.key.target.target_type_guid ||
      !value.key.first_property().is_valid() ||
      validate_value_operation(value.operation).failed()) {
    return error::invalid_argument;
  }
  auto field =
      meta::describe_property(value.key.target.target_type_guid, value.key.first_property());
  if (!field)
    return field.error();
  if (!has_flags(field->capabilities, meta::property_capabilities::writable) ||
      has_flags(field->flags, meta::property_flags::editor_read_only) || value.key.path.size() > 64)
    return error::invalid_argument;
  auto type = field->type;
  for (const auto& step : std::span{value.key.path}.subspan(1)) {
    if (const auto* property = std::get_if<meta::property_id>(&step)) {
      auto properties = type.properties();
      if (!properties)
        return properties.error();
      auto found = std::ranges::find(*properties, *property, &meta::property_info::id);
      if (found == properties->end())
        return error::not_found;
      if (!has_flags(found->capabilities, meta::property_capabilities::writable) ||
          has_flags(found->flags, meta::property_flags::editor_read_only))
        return error::invalid_argument;
      type = found->type;
    } else {
      if (type.kind() != (std::holds_alternative<std::string>(step) ? meta::type_kind::associative
                                                                    : meta::type_kind::sequence))
        return error::invalid_argument;
      if (std::holds_alternative<std::size_t>(step) && !type.is_fixed_sequence())
        return error::unsupported;
      if (const auto* id = std::get_if<guid>(&step); id && (!*id || !type.has_sequence_identity()))
        return error::invalid_argument;
      auto element = type.element_type();
      if (!element)
        return element.error();
      type = *element;
    }
  }
  if (!std::holds_alternative<set_value_operation>(value.operation)) {
    if (std::holds_alternative<put_value_operation>(value.operation) ||
        std::holds_alternative<erase_key_operation>(value.operation))
      return type.kind() == meta::type_kind::associative ? error::success : error::invalid_argument;
    return type.kind() == meta::type_kind::sequence && type.has_sequence_identity() &&
                   !type.is_fixed_sequence()
               ? error::success
               : error::invalid_argument;
  }
  if (validate_persistent_value(value.replacement()).failed())
    return error::invalid_argument;
  switch (type.kind()) {
  case meta::type_kind::scalar:
    if (!valid_value(value.replacement()) || type.scalar_kind() != value.replacement().kind())
      return error::invalid_argument;
    break;
  case meta::type_kind::object:
    if (value.replacement().kind() != value_kind::record ||
        std::get<record_value>(value.replacement().payload).get().type != type.guid())
      return error::invalid_argument;
    break;
  case meta::type_kind::sequence:
    if (value.replacement().kind() != value_kind::sequence)
      return error::invalid_argument;
    break;
  case meta::type_kind::associative:
    if (value.replacement().kind() != value_kind::map)
      return error::invalid_argument;
    break;
  case meta::type_kind::optional:
    if (value.replacement().kind() != value_kind::optional)
      return error::invalid_argument;
    break;
  case meta::type_kind::variant:
    if (value.replacement().kind() != value_kind::variant)
      return error::invalid_argument;
    break;
  case meta::type_kind::reference:
    if (value.replacement().kind() != value_kind::invalid &&
        value.replacement().kind() != value_kind::record &&
        value.replacement().kind() != value_kind::asset_reference)
      return error::invalid_argument;
    break;
  default:
    return error::unsupported;
  }
  return error::success;
} catch (...) {
  return error::internal;
}

error set_prefab_property_override(std::vector<prefab_property_override>& overrides,
                                   prefab_property_override candidate,
                                   engine::value source_value) noexcept {
  if (!std::holds_alternative<set_value_operation>(candidate.operation))
    return error::invalid_argument;
  auto operation = validate_prefab_property_override(candidate);
  if (operation != error::success) {
    return operation;
  }
  if (validate_persistent_value(source_value).failed())
    return error::invalid_argument;
  if (is_scalar_value_kind(source_value.kind()) &&
      is_scalar_value_kind(candidate.replacement().kind())) {
    if (!valid_value(source_value) || source_value.kind() != candidate.replacement().kind())
      return error::invalid_argument;
    normalize_zero(source_value);
    normalize_zero(candidate.replacement());
  }
  try {
    auto pending = overrides;
    // 整项 set 覆盖同一路径及其子路径的旧编辑；其他操作保持原顺序。
    std::erase_if(pending, [&](const auto& old) {
      return old.key.target == candidate.key.target &&
             old.key.path.size() >= candidate.key.path.size() &&
             std::equal(candidate.key.path.begin(), candidate.key.path.end(), old.key.path.begin());
    });
    if (candidate.replacement() != source_value)
      pending.push_back(std::move(candidate));
    overrides.swap(pending);
    return error::success;
  } catch (const std::bad_alloc&) {
    return error::out_of_memory;
  } catch (...) {
    return error::internal;
  }
}

namespace {
engine::error apply_candidate_object(const prefab_property_override& value,
                                     const engine::value& native, game_object_description& object) {
  auto result = engine::error::success;
  const auto is_type = [&](guid type) { return value.key.target.target_type_guid == type; };
  if (is_type(transform_component_type)) {
    auto transform = object.transform;
    result = meta::write_property(value.key.target.target_type_guid, value.key.first_property(),
                                  &transform, native);
    if (result != engine::error::success) {
      return result;
    }
    object.transform = transform;
  } else {
    if (value.key.path.size() != 1)
      return error::unsupported;
    return edit_component(object.components, value.key.target.target_type_guid,
                          value.key.first_property(), native);
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
    auto target = std::ranges::find(candidate.objects, key.first, &game_object_description::uuid);
    if (target == candidate.objects.end())
      return fail(error::not_found, key.first, key.second, {}, "覆盖目标对象已不存在");
    if (std::ranges::any_of(reference.overrides, [&](const auto& v) {
          return v.key.target.source_node_uuid == key.first &&
                 component_description_type(v.key.target.target_type_guid) == key.second &&
                 v.key.target.component_uuid == change.component_uuid;
        }))
      return fail(error::invalid_argument, key.first, key.second, {}, "结构覆盖与属性覆盖矛盾");
    if (change.added) {
      if (!*change.added || (*change.added)->guid() != key.second)
        return fail(error::invalid_argument, key.first, key.second, {}, "新增描述类型不匹配");
      if (target->components.find(key.second))
        return fail(error::invalid_state, key.first, key.second, {},
                    "来源已存在实例新增的同类型组件");
      auto entries = target->components.entries();
      auto identified = with_component_identity(*change.added, change.component_uuid);
      if (!identified)
        return identified.error();
      entries.push_back(std::move(*identified));
      auto status = target->components.replace(entries);
      if (status.failed())
        return status;
    } else {
      if (!change.component_uuid ||
          target->components.identity(key.second) != change.component_uuid)
        return fail(error::not_found, key.first, key.second, {}, "来源组件身份已失效");
      target->components.erase(key.second);
    }
  }
  // 提前定位每条属性覆盖；成组应用仍在下一步统一校验跨字段约束。
  for (const auto& entry : reference.overrides) {
    const auto& key = entry.key;
    std::string path = meta::property_path_text(key.path);
    auto object = std::ranges::find(candidate.objects, key.target.source_node_uuid,
                                    &game_object_description::uuid);
    auto status = validate_prefab_property_override(entry);
    if (status.failed() || object == candidate.objects.end())
      return fail(status.failed() ? status : error{error::not_found}, key.target.source_node_uuid,
                  key.target.target_type_guid, path, "属性覆盖目标不存在或类型不匹配");
    if (key.target.target_type_guid != transform_component_type) {
      auto* component =
          object->components.find(component_description_type(key.target.target_type_guid));
      if (!component || !key.target.component_uuid ||
          object->components.identity(component_description_type(key.target.target_type_guid)) !=
              key.target.component_uuid)
        return fail(error::not_found, key.target.source_node_uuid, key.target.target_type_guid,
                    path, "属性覆盖对应的来源组件已不存在");
      // 路径在顺序应用时定位：前一条 insert 可能刚创建本条操作的目标。
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
    auto found = std::ranges::find(candidate.objects, value.key.target.source_node_uuid,
                                   &game_object_description::uuid);
    if (found == candidate.objects.end()) {
      return engine::error::not_found;
    }
    if (found->uuid == candidate.root_uuid &&
        value.key.target.target_type_guid == transform_component_type)
      return error::invalid_argument;
    if (value.key.target.target_type_guid != transform_component_type &&
        (!value.key.target.component_uuid ||
         found->components.identity(component_description_type(
             value.key.target.target_type_guid)) != value.key.target.component_uuid))
      return error::not_found;
    // 描述类型的全部叶覆盖在同一副本中应用，再检查跨字段约束。
    if (found->components.find(value.key.target.target_type_guid))
      continue;
    if (!std::holds_alternative<set_value_operation>(value.operation))
      return error::unsupported;
    const auto* scalar = &value.replacement();
    if (!is_scalar_value_kind(scalar->kind()) || value.key.path.size() != 1)
      return error::unsupported;
    const auto result = apply_candidate_object(value, *scalar, *found);
    if (result != engine::error::success) {
      return result;
    }
  }
  for (auto& object : candidate.objects) {
    for (const auto& component : object.components) {
      const auto matches = [&](const prefab_property_override& entry) {
        return entry.key.target.source_node_uuid == object.uuid &&
               entry.key.target.target_type_guid == component->guid();
      };
      if (!std::ranges::any_of(overrides, matches))
        continue;
      auto changed = modify_component(component, [&](meta::mutable_instance root) -> error {
        auto captured = serialization::capture_value(root.as_const());
        if (!captured)
          return captured.error().error;
        std::vector<value_edit> edits;
        for (const auto& entry : overrides)
          if (matches(entry))
            edits.push_back({entry.key.path, entry.operation});
        auto updated = apply_value_edits(*captured, edits);
        if (!updated)
          return updated.error().error;
        return serialization::apply_value(root, *updated).error;
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

result<std::vector<prefab_property_override>>
prepare_mesh_renderer_overrides(guid source_node, guid component_uuid,
                                const mesh_renderer_description& original,
                                const mesh_renderer_description& replacement,
                                std::span<const prefab_property_override> existing) noexcept try {
  std::vector<prefab_property_override> pending(existing.begin(), existing.end());
  const std::array originals{original.mesh_uri, original.material_uri};
  const std::array replacements{replacement.mesh_uri, replacement.material_uri};
  for (std::size_t i = 0; i < originals.size(); ++i) {
    auto status = set_prefab_property_override(
        pending,
        {.key = {.target = {.source_node_uuid = source_node,
                            .target_type_guid = mesh_renderer_description::type_guid,
                            .component_uuid = component_uuid},
                 .path = {meta::property_id{static_cast<std::uint32_t>(i + 1)}}},
         .operation = engine::value{replacements[i]}},
        engine::value{originals[i]});
    if (status.failed())
      return status;
  }
  return pending;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}

} // namespace gneiss::engine
