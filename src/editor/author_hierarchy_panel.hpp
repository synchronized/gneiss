// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "editor_session.hpp"

#include <span>
#include <string>

namespace gneiss::editor {

enum class author_hierarchy_action {
  none,
  select,
  rename,
  duplicate,
  remove,
  create_prefab,
  reparent
};

/** 单次绘制借用的树快照；调用期间不得刷新节点数组。 */
struct author_hierarchy_view final {
  std::span<const scene_node_record> nodes;
  std::span<const prefab_node_record> prefab_nodes;
  scene_node_id selection;
  bool can_create_prefab = false;
};

/** 拥有操作身份；主线程在完整树绘制结束后处理，禁止在递归遍历时重建节点列表。 */
struct author_hierarchy_request final {
  author_hierarchy_action action = author_hierarchy_action::none;
  scene_node_id node;
  std::string uuid;
  std::string parent_uuid;
};

[[nodiscard]] author_hierarchy_request draw_author_hierarchy(const author_hierarchy_view& view);

} // namespace gneiss::editor
