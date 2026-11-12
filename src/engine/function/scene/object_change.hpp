// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/entity_id.hpp"
#include "engine/core/type_id.hpp"
#include <map>
namespace gneiss {
/** 变化表示需要重新读取，不保留组件借用；destroyed 优先于其他标记。 */
struct object_change {
  bool transform{};
  bool destroyed{};
  // 移除后重新添加仍保留 true。
  std::map<type_id, bool> components;
};
using object_changes = std::map<entity_id, object_change>;
}
