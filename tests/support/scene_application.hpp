// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/function/scene/scene_update.hpp"
#include "engine/resource/scene.hpp"
namespace gneiss::test_support {
inline std::map<gneiss::object_address, gneiss::scene_object_association>
associations(std::span<const gneiss::scene_object_binding> objects) {
  std::map<gneiss::object_address, gneiss::scene_object_association> output;
  for (const auto& object : objects)
    output.emplace(gneiss::object_address{object.object, object.source},
                   gneiss::scene_object_association{object.entity});
  return output;
}
inline gneiss::result<gneiss::composed_scene> compose(const gneiss::scene_content& input) {
  std::map<std::string, const gneiss::prefab_description*, std::less<>> sources;
  for (const auto& [uri, prefab] : input.prefabs)
    sources.emplace(uri, prefab.description.get());
  if (!input.description)
    return gneiss::error::invalid_argument;
  return gneiss::compose_scene(*input.description, sources);
}
} // namespace gneiss::test_support
