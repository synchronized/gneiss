// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "scene_validation.hpp"
#include "engine/core/serialization/json.hpp"
#include "engine/resource/asset_uri.hpp"
#include "engine/resource/scene/object_hierarchy.hpp"
#include <algorithm>
#include <set>
namespace gneiss::engine {
serialization::diagnostic validate_component_description(const component_description& value) {
  if (!value || !value->uuid)
    return {.path = "/uuid", .error = error::invalid_argument};
  const auto type = value->dynamic_type();
  if (!type.registered())
    return {.message = "组件描述类型未注册", .error = error::not_found};
  const auto* policy = type.extension<component_validation_policy>();
  return policy && policy->validate ? policy->validate(*value) : serialization::diagnostic{};
}
serialization::diagnostic validate_camera_description(const camera_description& value) {
  if (!math::is_valid_camera(value.settings))
    return {
        .path = "/settings", .message = "相机透视参数范围错误", .error = error::invalid_argument};
  return {};
}
serialization::diagnostic validate_property_override_key(const prefab_property_override_key& key) {
  if (!key.target.source_node_uuid)
    return {.path = "/target/source_node_uuid", .error = error::invalid_argument};
  if (!key.target.target_type_guid)
    return {.path = "/target/target_type_guid", .error = error::invalid_argument};
  if (!key.first_property().is_valid())
    return {.path = "/path", .error = error::invalid_argument};
  if (key.path.size() > 64)
    return {.path = "/path", .error = error::invalid_argument};
  for (const auto& step : key.path) {
    if (step.valueless_by_exception())
      return {.path = "/path", .error = error::invalid_argument};
    if (const auto* id = std::get_if<guid>(&step); id && !*id)
      return {.path = "/path", .error = error::invalid_argument};
    if (const auto* property = std::get_if<meta::property_id>(&step);
        property && !property->is_valid())
      return {.path = "/path", .error = error::invalid_argument};
  }
  return {};
}

serialization::diagnostic validate_component_override(const prefab_component_override& value) {
  if (!value.source_node_uuid)
    return {.path = "/source_node_uuid",
            .message = "组件覆盖对象不能为空",
            .error = error::invalid_argument};
  if (!value.component_uuid)
    return {.path = "/component_uuid",
            .message = "组件实例身份不能为空",
            .error = error::invalid_argument};
  if (!value.type_guid)
    return {
        .path = "/type_guid", .message = "组件覆盖类型不能为空", .error = error::invalid_argument};
  if (value.added && (!*value.added || (*value.added)->guid() != value.type_guid))
    return {.path = "/added",
            .message = "新增组件必须非空且匹配覆盖类型",
            .error = error::invalid_argument};
  if (value.added) {
    auto status = validate_component_description(*value.added);
    if (status.error.failed())
      status.path.insert(0, "/added");
    return status;
  }
  return {};
}

serialization::diagnostic
validate_prefab_reference_description(const prefab_reference_description& value) {
  if (validate_uri(value.uri).failed())
    return {
        .path = "/uri", .message = "Prefab 来源必须包含规范 URI", .error = error::invalid_argument};
  for (std::size_t i = 0; i < value.overrides.size(); ++i) {
    const auto& entry = value.overrides[i];
    auto key_status = validate_property_override_key(entry.key);
    if (key_status.error.failed()) {
      key_status.path.insert(0, "/overrides/" + std::to_string(i) + "/key");
      return key_status;
    }
    if (validate_value_operation(entry.operation).failed())
      return {.path = "/overrides/" + std::to_string(i),
              .message = "覆盖作者键或属性值无效",
              .error = error::invalid_argument};
  }
  std::set<std::pair<guid, guid>> components;
  for (std::size_t i = 0; i < value.component_overrides.size(); ++i) {
    const auto& entry = value.component_overrides[i];
    auto component_status = validate_component_override(entry);
    if (component_status.error.failed()) {
      component_status.path.insert(0, "/component_overrides/" + std::to_string(i));
      return component_status;
    }
    if (!components.emplace(entry.source_node_uuid, entry.type_guid).second ||
        std::ranges::any_of(value.overrides, [&](const auto& v) {
          return v.key.target.source_node_uuid == entry.source_node_uuid &&
                 v.key.target.target_type_guid == entry.type_guid &&
                 v.key.target.component_uuid == entry.component_uuid;
        }))
      return {.path = "/component_overrides/" + std::to_string(i),
              .message = "组件覆盖重复或与属性覆盖矛盾",
              .error = error::invalid_argument};
  }
  return {};
}
serialization::diagnostic validate_game_object_description(const game_object_description& object) {
  if (!object.uuid)
    return {.path = "/uuid", .message = "对象 UUID 不能为空", .error = error::invalid_argument};
  if (!math::is_valid_transform(object.transform))
    return {.path = "/transform", .message = "对象变换无效", .error = error::invalid_argument};
  std::size_t index = 0;
  for (const auto& component : object.components) {
    auto status = validate_component_description(component);
    if (status.error.failed()) {
      status.path.insert(0, "/components/" + std::to_string(index));
      return status;
    }
    ++index;
  }
  if (object.prefab && !object.components.empty())
    return {.path = "/components",
            .message = "Prefab 引用与独立组件不能同时定义",
            .error = error::invalid_argument};
  if (object.prefab) {
    auto status = validate_prefab_reference_description(*object.prefab);
    if (status.error.failed())
      status.path.insert(0, "/prefab");
    return status;
  }
  return {};
}
serialization::diagnostic
validate_mesh_renderer_description(const mesh_renderer_description& value) {
  if ((!value.mesh_uri.empty() && validate_uri(value.mesh_uri).failed()) ||
      (!value.material_uri.empty() && validate_uri(value.material_uri).failed()))
    return {.message = "资源引用必须是规范 asset URI", .error = error::invalid_argument};
  return {};
}

namespace {
serialization::diagnostic validate_hierarchy(std::span<const game_object_description> objects,
                                             std::optional<guid> root) {
  for (std::size_t i = 0; i < objects.size(); ++i) {
    auto status = validate_game_object_description(objects[i]);
    if (status.error.failed()) {
      status.path.insert(0, "/objects/" + std::to_string(i));
      return status;
    }
  }
  auto inspected = inspect_object_hierarchy(objects, root);
  if (!inspected) {
    auto diagnostic = std::move(inspected).error();
    return {diagnostic.byte_offset, std::move(diagnostic.path), std::move(diagnostic.message),
            diagnostic.error};
  }
  if (std::ranges::count_if(objects, [](const auto& object) {
        const auto* camera = object.components.template get<camera_description>();
        return camera && camera->is_primary;
      }) > 1)
    return {.path = "/objects",
            .message = "场景最多只能包含一个主相机",
            .error = error::invalid_argument};
  if (root &&
      std::ranges::any_of(objects, [](const auto& object) { return object.prefab.has_value(); }))
    return {.path = "/objects", .message = "暂不支持嵌套 Prefab 引用", .error = error::unsupported};
  return {};
}
}
serialization::diagnostic validate_scene_description(const scene_description& scene) {
  if (!scene.uuid)
    return {
        .path = "/scene_uuid", .message = "场景 UUID 不能为空", .error = error::invalid_argument};
  return validate_hierarchy(scene.objects, std::nullopt);
}
serialization::diagnostic validate_prefab_description(const prefab_description& prefab) {
  if (!prefab.uuid)
    return {.path = "/prefab_uuid",
            .message = "Prefab UUID 不能为空",
            .error = error::invalid_argument};
  if (!prefab.root_uuid)
    return {.path = "/root_uuid", .message = "根 UUID 不能为空", .error = error::invalid_argument};
  return validate_hierarchy(prefab.objects, prefab.root_uuid);
}
}
