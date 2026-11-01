// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/result.hpp"
#include "engine/resource/scene/scene_description.hpp"
#include <functional>
#include <limits>
#include <span>
#include <unordered_map>

namespace gneiss::engine {
/** 一次校验的临时结果，不持有对象或维护运行时树。根深度为零。 */
struct object_hierarchy final {
  std::unordered_map<guid, std::size_t, guid_hash> indices;
  std::vector<std::size_t> parent_first;
  std::vector<std::size_t> depths;
};
/** 无指定根时允许森林；指定根时要求全部对象归属该唯一根。 */
inline result<object_hierarchy, scene_diagnostic>
inspect_object_hierarchy(std::span<const object_description> objects,
                         std::optional<guid> root = std::nullopt,
                         std::size_t maximum_depth = std::numeric_limits<std::size_t>::max(),
                         const std::function<bool()>& cancelled = {}) noexcept try {
  const auto fail = [](std::string path,
                       std::string message) -> result<object_hierarchy, scene_diagnostic> {
    return scene_diagnostic{
        .error = error::invalid_argument, .path = std::move(path), .message = std::move(message)};
  };
  object_hierarchy output;
  output.depths.resize(objects.size());
  output.indices.reserve(objects.size());
  std::vector<std::vector<std::size_t>> children(objects.size());
  for (std::size_t i = 0; i < objects.size(); ++i) {
    if (cancelled && cancelled())
      return scene_diagnostic{.error = error::invalid_state};
    if (!objects[i].uuid || !output.indices.emplace(objects[i].uuid, i).second)
      return fail("/objects/" + std::to_string(i) + "/uuid", "对象 UUID 无效或重复");
  }
  for (std::size_t i = 0; i < objects.size(); ++i) {
    if (cancelled && cancelled())
      return scene_diagnostic{.error = error::invalid_state};
    const auto parent = objects[i].parent_uuid;
    if (!parent)
      output.parent_first.push_back(i);
    else {
      const auto found = output.indices.find(*parent);
      if (!*parent || found == output.indices.end())
        return fail("/objects/" + std::to_string(i) + "/parent", "父对象不存在");
      children[found->second].push_back(i);
    }
  }
  if (root) {
    if (!*root || !output.indices.contains(*root) || objects[output.indices.at(*root)].parent_uuid)
      return fail("/root_uuid", "指定根必须存在且无父对象");
    if (output.parent_first.size() != 1)
      return fail("/objects", "Prefab 必须且只能包含一个根节点");
  }
  for (std::size_t cursor = 0; cursor < output.parent_first.size(); ++cursor) {
    if (cancelled && cancelled())
      return scene_diagnostic{.error = error::invalid_state};
    const auto i = output.parent_first[cursor];
    if (output.depths[i] >= maximum_depth)
      return fail("/objects/" + std::to_string(i) + "/parent", "对象层级超过深度限制");
    for (auto child : children[i]) {
      output.depths[child] = output.depths[i] + 1;
      output.parent_first.push_back(child);
    }
  }
  if (output.parent_first.size() != objects.size()) {
    std::vector<bool> visited(objects.size());
    for (auto i : output.parent_first)
      visited[i] = true;
    for (std::size_t i = 0; i < objects.size(); ++i)
      if (!visited[i])
        return fail("/objects/" + std::to_string(i) + "/parent", "对象层级形成循环");
  }
  return output;
} catch (const std::bad_alloc&) {
  return scene_diagnostic{.error = error::out_of_memory};
} catch (...) {
  return scene_diagnostic{.error = error::internal};
}
}
