// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "ipc_inspection_protocol.h"
#include "runtime_scene_data.hpp"

#include <new>
#include <utility>

namespace gneiss::editor {

/** 消费已解码批次；成功移动节点文本，失败保持 output 不变，输入可被消费。 */
[[nodiscard]] inline result to_runtime_scene_batch(ipc_inspection_batch&& input,
                                                   runtime_scene_batch& output) noexcept {
  try {
    runtime_scene_batch converted{.stamp = {input.stamp.session_id, input.stamp.sequence},
                                  .is_full = input.is_full,
                                  .chunk_index = input.chunk_index,
                                  .chunk_count = input.chunk_count,
                                  .changes = {}};
    converted.changes.reserve(input.changes.size());
    for (auto& change : input.changes) {
      runtime_scene_change_type type;
      switch (change.type) {
      case ipc_inspection_change_type::upsert:
        type = runtime_scene_change_type::upsert;
        break;
      case ipc_inspection_change_type::remove:
        type = runtime_scene_change_type::remove;
        break;
      default:
        return result::invalid_argument;
      }
      auto& node = change.node;
      converted.changes.push_back(
          {.type = type,
           .id = {change.id.value, change.id.generation},
           .node = {.id = {node.id.value, node.id.generation},
                    .parent = {node.parent.value, node.parent.generation},
                    .uuid = std::move(node.uuid),
                    .prefab_instance_uuid = std::move(node.prefab_instance_uuid),
                    .prefab_source_node_uuid = std::move(node.prefab_source_node_uuid),
                    .name = std::move(node.name),
                    .local_transform = node.local_transform,
                    .component_flags = node.component_flags,
                    .camera = node.camera,
                    .mesh_uri = std::move(node.mesh_uri),
                    .material_uri = std::move(node.material_uri)}});
    }
    output = std::move(converted);
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

} // namespace gneiss::editor
