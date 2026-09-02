// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_author_apply.h"

#include <string>

namespace gneiss::editor {

result apply_runtime_transform_to_author(editor_session& session, editor_command_history& history,
                                         const ipc_inspection_node& runtime_node) noexcept {
  if (runtime_node.uuid.empty()) {
    return result::invalid_argument;
  }
  const auto* author_node = session.find_node(runtime_node.uuid);
  if (author_node == nullptr) {
    return result::not_found;
  }
  const auto uuid = runtime_node.uuid;
  const auto before = author_node->local_transform;
  const auto after = runtime_node.local_transform;
  return history.execute({.label = "应用 Runtime Transform",
                          .undo =
                              [&session, uuid, before] {
                                const auto* node = session.find_node(uuid);
                                return node == nullptr
                                           ? result::not_found
                                           : session.set_local_transform(node->node, before);
                              },
                          .redo =
                              [&session, uuid, after] {
                                const auto* node = session.find_node(uuid);
                                return node == nullptr
                                           ? result::not_found
                                           : session.set_local_transform(node->node, after);
                              },
                          .merge_key = {}});
}

} // namespace gneiss::editor
