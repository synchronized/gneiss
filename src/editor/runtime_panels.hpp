// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "runtime_property_edits.hpp"
#include "runtime_scene_mirror.hpp"

namespace gneiss::editor {

/** 窗口拥有的 Runtime 选择；会话切换后不再匹配旧对象。 */
struct runtime_scene_selection final {
  runtime_object_id object;
  std::uint64_t session = 0U;
};

/** 单次绘制借用的宿主操作；所有回调必须提供，只在 Editor 主线程同步调用。
 * context、节点和属性指针不得逃逸本次绘制；面板不拥有进程、协议或属性状态。 */
struct runtime_inspector_actions final {
  void* context = nullptr;
  bool editable = false;
  bool can_apply_to_author = false;
  const runtime_property_edit* (*find_edit)(void*, const runtime_property_key&) = nullptr;
  result (*write_property)(void*, const runtime_property_key&, std::uint64_t,
                           runtime_property_value) = nullptr;
  void (*apply_to_author)(void*, const runtime_scene_node&) = nullptr;
  void (*report_result)(void*, result) = nullptr;
};

/** 返回镜像中借用的选择节点；镜像更新后必须重新查询。 */
[[nodiscard]] const runtime_scene_node*
selected_runtime_node(const runtime_scene_mirror& mirror,
                      const runtime_scene_selection& selection) noexcept;
void draw_runtime_hierarchy(const runtime_scene_mirror& mirror, runtime_scene_selection& selection);
void draw_runtime_inspector(const runtime_scene_node& node,
                            const runtime_inspector_actions& actions);

} // namespace gneiss::editor
