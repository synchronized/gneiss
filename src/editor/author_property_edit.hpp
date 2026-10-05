// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "editor_command_history.hpp"
#include "editor_session.hpp"
#include "property_inspector_model.hpp"

namespace gneiss::editor {

/** 编辑当前作者节点并记录撤销命令；仅限 Editor 主线程。
 * inspector 必须已刷新为当前选择，组件和属性必须来自该快照。
 * 输入只在调用期间借用，命令复制 UUID 和属性值。
 * session、inspector 和 world 必须活到历史清空；文档关闭前由宿主清空历史。
 * 写入或历史记录失败时不标脏；记录失败会恢复旧值。 */
[[nodiscard]] result edit_author_property(editor_session& session, editor_command_history& history,
                                          property_inspector_model& inspector, gneiss_world world,
                                          const inspector_component& component,
                                          const inspector_property& property,
                                          const gneiss_property_value& value,
                                          std::uint64_t edit_serial) noexcept;

} // namespace gneiss::editor
