// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "commands/editor_command_history.hpp"
#include "documents/scene_document.hpp"
#include "engine/core/error.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/world/world.hpp"
#include "panels/property_inspector_model.hpp"
#include "panels/property_path.hpp"

namespace gneiss::editor {
/** 稳定作者身份；普通节点使用 object_uuid，Prefab 使用 instance/source 二元身份。 */
struct component_edit_target {
  engine::guid object_uuid{}, instance_uuid{}, source_uuid{};
  engine::guid component_type{};
};
/** 当前支持 Camera；只读主相机状态不能通过属性操作改变。
 * 复制候选并校验，历史按稳定身份重新定位，不借用成员指针。
 * document/world 须存活至历史清空；调用限所属线程。 */
[[nodiscard]] engine::error edit_component_description(scene_document&, editor_command_history&,
                                                       engine::world&, const component_edit_target&,
                                                       const property_edit&,
                                                       std::uint64_t serial) noexcept;

/** 编辑当前作者节点并记录撤销命令；仅限 Editor 主线程。
 * inspector 必须已刷新为当前选择，组件和属性必须来自该快照。
 * 输入只在调用期间借用，命令复制 UUID 和属性值。
 * session、inspector 和 world 必须活到历史清空；文档关闭前由宿主清空历史。
 * 写入或历史记录失败时不标脏；记录失败会恢复旧值。 */
[[nodiscard]] gneiss::engine::error
edit_author_property(scene_document& session, editor_command_history& history,
                     property_inspector_model& inspector, gneiss::engine::world* world,
                     const inspector_component& component, const inspector_property& property,
                     const gneiss::engine::value& value, std::uint64_t edit_serial) noexcept;

} // namespace gneiss::editor
