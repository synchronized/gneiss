// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_scene_mirror.h"

#include <new>
#include <set>
#include <utility>

namespace {

bool valid_graph(const std::map<std::uint64_t, gneiss::ipc_inspection_node>& nodes) {
  std::set<std::string, std::less<>> uuids;
  for (const auto& [value, node] : nodes) {
    if (!node.id.is_valid() || node.id.value != value || node.uuid.empty() ||
        !uuids.insert(node.uuid).second) {
      return false;
    }
    if (node.parent.is_valid()) {
      const auto parent = nodes.find(node.parent.value);
      if (parent == nodes.end() || parent->second.id != node.parent ||
          parent->second.id == node.id) {
        return false;
      }
    }
    auto ancestor = node.parent;
    std::size_t depth = 0U;
    while (ancestor.is_valid()) {
      if (++depth > nodes.size()) {
        return false;
      }
      const auto found = nodes.find(ancestor.value);
      if (found == nodes.end() || found->second.id != ancestor) {
        return false;
      }
      ancestor = found->second.parent;
    }
  }
  return true;
}

} // namespace

namespace gneiss::editor {

result runtime_scene_mirror::apply(const ipc_inspection_batch& batch) noexcept {
  if (batch.stamp.session_id == 0U || batch.stamp.sequence == 0U) {
    return result::invalid_argument;
  }
  try {
    if (batch.is_full) {
      if (sequence_.begin(batch.stamp.session_id, batch.stamp.sequence) != result::success) {
        return result::invalid_argument;
      }
    } else if (needs_full_snapshot_) {
      return result::not_ready;
    }
    const auto order = sequence_.observe(batch.stamp);
    if (order == ipc_inspection_sequence_result::duplicate) {
      return result::success;
    }
    if (order != ipc_inspection_sequence_result::accepted) {
      needs_full_snapshot_ = true;
      return order == ipc_inspection_sequence_result::invalid ? result::invalid_argument
                                                              : result::not_ready;
    }

    std::map<std::uint64_t, ipc_inspection_node> pending =
        batch.is_full ? decltype(by_id_){} : by_id_;
    for (const auto& change : batch.changes) {
      if (!change.id.is_valid()) {
        needs_full_snapshot_ = true;
        return result::invalid_argument;
      }
      if (change.type == ipc_inspection_change_type::remove) {
        const auto found = pending.find(change.id.value);
        if (found == pending.end() || found->second.id != change.id) {
          needs_full_snapshot_ = true;
          return result::not_ready;
        }
        pending.erase(found);
      } else {
        if (change.node.id != change.id) {
          needs_full_snapshot_ = true;
          return result::invalid_argument;
        }
        pending.insert_or_assign(change.id.value, change.node);
      }
    }
    if (!valid_graph(pending)) {
      needs_full_snapshot_ = true;
      return result::invalid_argument;
    }
    by_id_ = std::move(pending);
    rebuild_nodes();
    needs_full_snapshot_ = false;
    return result::success;
  } catch (const std::bad_alloc&) {
    needs_full_snapshot_ = true;
    return result::out_of_memory;
  } catch (...) {
    needs_full_snapshot_ = true;
    return result::internal;
  }
}

void runtime_scene_mirror::reset() noexcept {
  sequence_.reset();
  by_id_.clear();
  nodes_.clear();
  needs_full_snapshot_ = true;
}

void runtime_scene_mirror::rebuild_nodes() {
  std::vector<ipc_inspection_node> pending;
  pending.reserve(by_id_.size());
  for (const auto& [id, node] : by_id_) {
    (void)id;
    pending.push_back(node);
  }
  nodes_ = std::move(pending);
}

} // namespace gneiss::editor
