// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_RUNTIME_AUTHOR_APPLY_H_
#define GNEISS_APPS_EDITOR_RUNTIME_AUTHOR_APPLY_H_

#include "editor_command_history.hpp"
#include "editor_session.hpp"
#include <string_view>

namespace gneiss::editor {

/** 调用期间借用身份字符串；成功记录的撤销命令持有自己的副本。 */
struct runtime_author_transform final {
  std::string_view uuid;
  std::string_view prefab_instance_uuid;
  std::string_view prefab_source_node_uuid;
  transform local_transform = GNEISS_TRANSFORM_IDENTITY;
};

/** 将具有持久化 UUID 映射的 Runtime Transform 作为可撤销命令应用到作者场景。 */
[[nodiscard]] result
apply_runtime_transform_to_author(editor_session& session, editor_command_history& history,
                                  const runtime_author_transform& runtime_node) noexcept;

} // namespace gneiss::editor

#endif
