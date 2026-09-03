// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_author_apply.h"

#include <string>
#include <string_view>

namespace {

gneiss::scene_node_id resolve_author_node(gneiss::editor::editor_session& session,
                                          std::string_view uuid, std::string_view instance_uuid,
                                          std::string_view source_uuid) noexcept {
  if (instance_uuid.empty()) {
    const auto* node = session.find_node(uuid);
    return node == nullptr ? gneiss::scene_node_id{} : node->node;
  }
  const auto* node = source_uuid.empty() ? session.find_prefab_root(instance_uuid)
                                         : session.find_prefab_source(instance_uuid, source_uuid);
  return node == nullptr ? gneiss::scene_node_id{} : node->node;
}

} // namespace

namespace gneiss::editor {

result apply_runtime_transform_to_author(editor_session& session, editor_command_history& history,
                                         const ipc_inspection_node& runtime_node) noexcept {
  if (runtime_node.uuid.empty()) {
    return result::invalid_argument;
  }
  const auto has_prefab_identity = !runtime_node.prefab_instance_uuid.empty();
  if (!has_prefab_identity && !runtime_node.prefab_source_node_uuid.empty()) {
    return result::invalid_argument;
  }
  const auto* author_node = has_prefab_identity ? nullptr : session.find_node(runtime_node.uuid);
  const auto* prefab_node =
      !has_prefab_identity
          ? nullptr
          : (runtime_node.prefab_source_node_uuid.empty()
                 ? session.find_prefab_root(runtime_node.prefab_instance_uuid)
                 : session.find_prefab_source(runtime_node.prefab_instance_uuid,
                                              runtime_node.prefab_source_node_uuid));
  if (author_node == nullptr && prefab_node == nullptr) {
    return result::not_found;
  }
  const auto uuid = runtime_node.uuid;
  const auto instance_uuid = runtime_node.prefab_instance_uuid;
  const auto source_uuid = runtime_node.prefab_source_node_uuid;
  const auto before =
      author_node != nullptr ? author_node->local_transform : prefab_node->local_transform;
  const auto after = runtime_node.local_transform;
  return history.execute(
      {.label = "应用 Runtime Transform",
       .undo =
           [&session, uuid, instance_uuid, source_uuid, before] {
             const auto node = resolve_author_node(session, uuid, instance_uuid, source_uuid);
             return !node.is_valid() ? result::not_found
                                     : session.set_local_transform(node, before);
           },
       .redo =
           [&session, uuid, instance_uuid, source_uuid, after] {
             const auto node = resolve_author_node(session, uuid, instance_uuid, source_uuid);
             return !node.is_valid() ? result::not_found : session.set_local_transform(node, after);
           },
       .merge_key = {}});
}

} // namespace gneiss::editor
