// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "commands/asset_scene_commands.hpp"
#include "engine/core/result.hpp"

#include <algorithm>
#include <memory>
#include <new>
#include <string>
#include <utility>

namespace gneiss::editor {
namespace {
gneiss::engine::result execute_asset_edit(scene_document& session, editor_command_history& history,
                                          editor_command_history::command command) noexcept {
  const auto was_dirty = session.is_dirty();
  const auto operation = history.execute(std::move(command));
  if (operation.failed() && !was_dirty) {
    session.clear_dirty();
  }
  return operation;
}
} // namespace

gneiss::engine::result add_mesh_asset(scene_document& session, editor_command_history& history,
                                      std::string_view name, mesh_asset_reference assets) noexcept
    try {
  scene_node_snapshot snapshot;
  const auto identity = make_editor_uuid(snapshot.uuid);
  if (identity.failed()) {
    return identity;
  }
  snapshot.display_name = name.empty() ? snapshot.uuid : std::string{name};
  snapshot.mesh_uri = assets.mesh_uri;
  snapshot.material_uri = assets.material_uri;
  return execute_asset_edit(session, history,
                            {
                                .label = "创建 Mesh Renderer 节点",
                                .undo =
                                    [&session, uuid = snapshot.uuid] {
                                      const auto* current = session.find_node(uuid);
                                      if (current == nullptr) {
                                        return gneiss::engine::result{
                                            gneiss::engine::result::not_found};
                                      }
                                      scene_node_snapshot discarded;
                                      return session.destroy_node(current->node, discarded);
                                    },
                                .redo =
                                    [&session, snapshot] {
                                      scene_node_id node;
                                      return session.restore_mesh_renderer_node(snapshot, node);
                                    },
                                .merge_key = {},
                            });
} catch (const std::bad_alloc&) {
  return gneiss::engine::result::out_of_memory;
} catch (...) {
  return gneiss::engine::result::internal;
}

gneiss::engine::result add_prefab_asset(scene_document& session, editor_command_history& history,
                                        std::string_view name, std::string_view prefab_uri,
                                        scene_node_id parent) noexcept try {
  auto snapshot = std::make_shared<prefab_instance_snapshot>();
  const auto identity = make_editor_uuid(snapshot->instance_uuid);
  if (identity.failed()) {
    return identity;
  }
  if (parent.is_valid()) {
    const auto found = std::ranges::find(session.nodes(), parent, &scene_node_record::node);
    if (found == session.nodes().end()) {
      return gneiss::engine::result::not_found;
    }
    snapshot->parent_uuid = found->uuid;
  }
  snapshot->display_name = name;
  snapshot->prefab_uri = prefab_uri;
  return execute_asset_edit(
      session, history,
      {
          .label = "放置 Prefab 实例",
          .undo =
              [&session, uuid = snapshot->instance_uuid, snapshot] {
                const auto* current = session.find_prefab_root(uuid);
                return current == nullptr
                           ? gneiss::engine::result{gneiss::engine::result::not_found}
                           : session.destroy_prefab_instance(current->node, *snapshot);
              },
          .redo =
              [&session, snapshot] {
                scene_node_id root;
                return session.restore_prefab_instance(*snapshot, root);
              },
          .merge_key = {},
      });
} catch (const std::bad_alloc&) {
  return gneiss::engine::result::out_of_memory;
} catch (...) {
  return gneiss::engine::result::internal;
}

gneiss::engine::result replace_mesh_assets(scene_document& session, editor_command_history& history,
                                           std::string_view node_uuid,
                                           mesh_asset_reference assets) noexcept try {
  const auto* node = session.find_node(node_uuid);
  if (node == nullptr) {
    return gneiss::engine::result::not_found;
  }
  return execute_asset_edit(
      session, history,
      {
          .label = "替换 Mesh Renderer 资源",
          .undo =
              [&session, uuid = node->uuid, mesh = node->mesh_uri, material = node->material_uri] {
                const auto* current = session.find_node(uuid);
                return current == nullptr
                           ? gneiss::engine::result{gneiss::engine::result::not_found}
                           : session.set_mesh_renderer(current->node, mesh, material);
              },
          .redo =
              [&session, uuid = node->uuid, mesh = std::string{assets.mesh_uri},
               material = std::string{assets.material_uri}] {
                const auto* current = session.find_node(uuid);
                return current == nullptr
                           ? gneiss::engine::result{gneiss::engine::result::not_found}
                           : session.set_mesh_renderer(current->node, mesh, material);
              },
          .merge_key = {},
      });
} catch (const std::bad_alloc&) {
  return gneiss::engine::result::out_of_memory;
} catch (...) {
  return gneiss::engine::result::internal;
}

} // namespace gneiss::editor
