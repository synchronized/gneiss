// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/function/scene/scene_application.hpp"
#include "engine/resource/scene.hpp"
namespace gneiss::test_support {
inline std::map<engine::object_address, engine::scene_object_association>
associations(std::span<const engine::scene_object_binding> objects) {
  std::map<engine::object_address, engine::scene_object_association> output;
  for (const auto& object : objects)
    output.emplace(engine::object_address{object.object, object.source},
                   engine::scene_object_association{object.node, object.entity});
  return output;
}
inline engine::result<engine::composed_scene> compose(const engine::scene_content& input) {
  std::map<std::string, const engine::prefab_description*, std::less<>> sources;
  for (const auto& [uri, prefab] : input.prefabs)
    sources.emplace(uri, prefab.description.get());
  if (!input.description)
    return engine::error::invalid_argument;
  return engine::compose_scene(*input.description, sources);
}
} // namespace gneiss::test_support
