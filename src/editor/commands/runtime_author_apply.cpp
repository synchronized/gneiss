// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "commands/runtime_author_apply.hpp"
#include "engine/core/error.hpp"

#include <new>
#include <string>
#include <string_view>

namespace {

gneiss::scene_node_id resolve_author_node(gneiss::editor::scene_document& session,
                                          gneiss::engine::guid uuid,
                                          gneiss::engine::guid instance_uuid,
                                          gneiss::engine::guid source_uuid) noexcept {
  if (instance_uuid.is_nil()) {
    const auto* node = session.find_node(uuid);
    return node == nullptr ? gneiss::scene_node_id{} : node->node;
  }
  const auto* node = source_uuid.is_nil() ? session.find_prefab_root(instance_uuid)
                                          : session.find_prefab_source(instance_uuid, source_uuid);
  return node == nullptr ? gneiss::scene_node_id{} : node->node;
}

} // namespace

namespace gneiss::editor {

gneiss::engine::error
apply_runtime_transform_to_author(scene_document& session, editor_command_history& history,
                                  const runtime_author_transform& runtime_node) noexcept try {
  if (runtime_node.uuid.is_nil()) {
    return gneiss::engine::error::invalid_argument;
  }
  const auto has_prefab_identity = !runtime_node.prefab_instance_uuid.is_nil();
  if (!has_prefab_identity && !runtime_node.prefab_source_node_uuid.is_nil()) {
    return gneiss::engine::error::invalid_argument;
  }
  const auto* author_node = has_prefab_identity ? nullptr : session.find_node(runtime_node.uuid);
  const auto* prefab_node =
      !has_prefab_identity
          ? nullptr
          : (runtime_node.prefab_source_node_uuid.is_nil()
                 ? session.find_prefab_root(runtime_node.prefab_instance_uuid)
                 : session.find_prefab_source(runtime_node.prefab_instance_uuid,
                                              runtime_node.prefab_source_node_uuid));
  if (author_node == nullptr && prefab_node == nullptr) {
    return gneiss::engine::error::not_found;
  }
  const gneiss::engine::guid uuid(runtime_node.uuid);
  const gneiss::engine::guid instance_uuid(runtime_node.prefab_instance_uuid);
  const gneiss::engine::guid source_uuid(runtime_node.prefab_source_node_uuid);
  const auto before =
      author_node != nullptr ? author_node->local_transform : prefab_node->local_transform;
  const auto after = runtime_node.local_transform;
  return history.execute({.label = "应用 Runtime Transform",
                          .undo =
                              [&session, uuid, instance_uuid, source_uuid, before] {
                                const auto node =
                                    resolve_author_node(session, uuid, instance_uuid, source_uuid);
                                return !node.is_valid() ? gneiss::engine::error::not_found
                                                        : session.set_local_transform(node, before);
                              },
                          .redo =
                              [&session, uuid, instance_uuid, source_uuid, after] {
                                const auto node =
                                    resolve_author_node(session, uuid, instance_uuid, source_uuid);
                                return !node.is_valid() ? gneiss::engine::error::not_found
                                                        : session.set_local_transform(node, after);
                              },
                          .merge_key = {}});
} catch (const std::bad_alloc&) {
  return gneiss::engine::error::out_of_memory;
} catch (...) {
  return gneiss::engine::error::internal;
}

} // namespace gneiss::editor
