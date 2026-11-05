// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "scene_validation.hpp"
#include "engine/resource/scene/object_hierarchy.hpp"
#include <algorithm>
namespace gneiss::engine {
namespace {
meta::serialization_diagnostic validate_hierarchy(std::span<const object_description> objects,
                                                  std::optional<guid> root) {
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
meta::serialization_diagnostic validate_scene_description(const scene_description& scene) {
  if (!scene.uuid)
    return {
        .path = "/scene_uuid", .message = "场景 UUID 不能为空", .error = error::invalid_argument};
  return validate_hierarchy(scene.objects, std::nullopt);
}
meta::serialization_diagnostic validate_prefab_description(const prefab_description& prefab) {
  if (!prefab.uuid)
    return {.path = "/prefab_uuid",
            .message = "Prefab UUID 不能为空",
            .error = error::invalid_argument};
  if (!prefab.root_uuid)
    return {.path = "/root_uuid", .message = "根 UUID 不能为空", .error = error::invalid_argument};
  return validate_hierarchy(prefab.objects, prefab.root_uuid);
}
}
