// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "commands/editor_command_history.hpp"
#include "documents/scene_document.hpp"
#include "engine/core/error.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/world/world.hpp"
#include "panels/property_path.hpp"

namespace gneiss::editor {
/** 稳定作者身份；普通节点使用 object_uuid，Prefab 使用 instance/source 二元身份。 */
struct component_edit_target {
  engine::guid object_uuid{}, instance_uuid{}, source_uuid{};
  engine::guid component_type{};
};
/** 支持已注册运行组件；资源 URI 交给异步资产命令，主相机状态保持只读。
 * 复制候选并校验，历史按稳定身份重新定位，不借用成员指针。
 * document/world 须存活至历史清空；调用限所属线程。 */
[[nodiscard]] engine::error edit_component_description(scene_document&, editor_command_history&,
                                                       engine::world&, const component_edit_target&,
                                                       const property_edit&,
                                                       std::uint64_t serial) noexcept;

} // namespace gneiss::editor
