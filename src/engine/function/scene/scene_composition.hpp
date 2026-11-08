// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/function/scene/prefab_replay.hpp"
#include "engine/resource/scene/object_address.hpp"
#include "engine/resource/scene/scene_description.hpp"
#include <map>
namespace gneiss::engine {
/** 一次组合的完整有效对象；不含 World 句柄，不是长期权威状态。 */
struct composed_object {
  object_address address, parent;
  game_object_description effective;
  std::string prefab_uri;
  math::transform source_transform{};
  bool instance_root = false;
  std::vector<prefab_property_override_key> overrides;
};
struct composed_scene {
  std::vector<composed_object> objects;
  std::map<guid, std::vector<prefab_override_conflict>> conflicts;
};
/** 纯内存组合，不读取文件或修改输入；sources 借用到本次调用结束，空指针视为来源缺失。
 * 保留覆盖冲突；输入描述须先通过领域校验。 */
[[nodiscard]] GNEISS_API result<composed_scene> compose_scene(
    const scene_description&,
    const std::map<std::string, const prefab_description*, std::less<>>& sources) noexcept;
}
