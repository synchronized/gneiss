// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "documents/scene_edit_session.hpp"
namespace gneiss::test_support {
inline engine::result<entity_id>
place_document_prefab(editor::scene_edit_session& document, engine::game_object_description author,
                      engine::prefab_asset_ref source,
                      std::span<const editor::scene_mesh_binding> bindings, bool replacing) {
  gneiss::editor::scene_subtree_snapshot snapshot;
  snapshot.root_uuid = author.uuid;
  snapshot.parent = author.parent;
  author.parent = {};
  engine::scene_description description;
  description.uuid = document.capture_content()->content.get().uuid;
  description.objects.push_back(author);
  auto content = engine::capture_scene_record(description);
  if (!content)
    return content.error();
  snapshot.content = *content;
  auto frozen = engine::capture_prefab_record(*source);
  if (!frozen)
    return frozen.error();
  snapshot.resources[author.uuid].prefab_content = *frozen;
  snapshot.resources[author.uuid].source_nodes.assign(bindings.begin(), bindings.end());
  auto status = document.apply_prefab(snapshot, false, replacing);
  if (status.failed())
    return status;
  auto node = document.find_prefab_root(author.uuid);
  return node ? engine::result<entity_id>{node->node} : engine::result<entity_id>{node.error()};
}
}
