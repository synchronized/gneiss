// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "commands/runtime_author_apply.hpp"
#include "documents/scene_document_edit.hpp"
#include <gneiss/engine/error.hpp>

#include <new>
#include <string>
#include <string_view>

namespace {

gneiss::entity_id resolve_author_node(gneiss::editor::scene_edit_session& session,
                                      gneiss::guid uuid, gneiss::guid instance_uuid,
                                      gneiss::guid source_uuid) {
  if (instance_uuid.is_nil()) {
    const auto node = session.find_node(uuid);
    return !node ? gneiss::entity_id{} : node->node;
  }
  const auto node = source_uuid.is_nil() ? session.find_prefab_root(instance_uuid)
                                         : session.find_prefab_source(instance_uuid, source_uuid);
  return !node ? gneiss::entity_id{} : node->node;
}

} // namespace

namespace gneiss::editor {

gneiss::error
apply_runtime_transform_to_author(scene_edit_session& session, editor_command_history& history,
                                  const runtime_author_transform& runtime_node) {
  if (runtime_node.uuid.is_nil() && runtime_node.prefab_instance_uuid.is_nil()) {
    return gneiss::error::invalid_argument;
  }
  const auto has_prefab_identity = !runtime_node.prefab_instance_uuid.is_nil();
  if (!has_prefab_identity && !runtime_node.prefab_source_node_uuid.is_nil()) {
    return gneiss::error::invalid_argument;
  }
  const auto author_node = has_prefab_identity
                               ? gneiss::error{gneiss::error::not_found}
                               : session.find_node(runtime_node.uuid);
  const auto prefab_node =
      !has_prefab_identity
          ? gneiss::error{gneiss::error::not_found}
          : (runtime_node.prefab_source_node_uuid.is_nil()
                 ? session.find_prefab_root(runtime_node.prefab_instance_uuid)
                 : session.find_prefab_source(runtime_node.prefab_instance_uuid,
                                              runtime_node.prefab_source_node_uuid));
  if (!author_node && !prefab_node) {
    return gneiss::error::not_found;
  }
  const gneiss::guid uuid(runtime_node.uuid);
  const gneiss::guid instance_uuid(runtime_node.prefab_instance_uuid);
  const gneiss::guid source_uuid(runtime_node.prefab_source_node_uuid);
  return execute_scene_edit(history, session, "应用 Runtime Transform", [&] {
    const auto node = resolve_author_node(session, uuid, instance_uuid, source_uuid);
    return !node.is_valid() ? gneiss::error::not_found
                            : session.set_local_transform(node, runtime_node.local_transform);
  });
}

} // namespace gneiss::editor
