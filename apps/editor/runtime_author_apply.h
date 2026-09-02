// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_RUNTIME_AUTHOR_APPLY_H_
#define GNEISS_APPS_EDITOR_RUNTIME_AUTHOR_APPLY_H_

#include "editor_command_history.h"
#include "editor_session.h"
#include "ipc_inspection_protocol.h"

namespace gneiss::editor {

/** 将具有持久化 UUID 映射的 Runtime Transform 作为可撤销命令应用到作者场景。 */
[[nodiscard]] result
apply_runtime_transform_to_author(editor_session& session, editor_command_history& history,
                                  const ipc_inspection_node& runtime_node) noexcept;

} // namespace gneiss::editor

#endif
