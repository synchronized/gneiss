// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/function/scene/scene_instances.hpp"
#include "value_result.hpp"
namespace gneiss::test_support {
inline engine::error prefab_node_count(engine::scene_instances& scenes, scene_instance_id scene,
                                       std::uint64_t* output) {
  *output = 0;
  std::uint64_t count{};
  auto status = gneiss::test_support::take_value(scenes.get_node_count(scene), count);
  if (status.failed())
    return status;
  for (std::uint64_t index = 0; index < count; ++index) {
    engine::scene_node_info node;
    status = gneiss::test_support::take_value(scenes.get_node_info(scene, index), node);
    if (status.failed())
      return status;
    if (node.prefab)
      ++*output;
  }
  return engine::error::success;
}
inline engine::error prefab_node_info(engine::scene_instances& scenes, scene_instance_id scene,
                                      std::uint64_t selected, engine::scene_node_info& output) {
  std::uint64_t count{}, position{};
  auto status = gneiss::test_support::take_value(scenes.get_node_count(scene), count);
  if (status.failed())
    return status;
  for (std::uint64_t index = 0; index < count; ++index) {
    engine::scene_node_info node;
    status = gneiss::test_support::take_value(scenes.get_node_info(scene, index), node);
    if (status.failed())
      return status;
    if (node.prefab && position++ == selected) {
      output = node;
      return engine::error::success;
    }
  }
  return engine::error::not_found;
}
}
