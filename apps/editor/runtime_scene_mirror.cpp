// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_scene_mirror.h"

#include <algorithm>
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
  if (batch.stamp.session_id == 0U || batch.stamp.sequence == 0U || batch.chunk_count == 0U ||
      batch.chunk_index >= batch.chunk_count) {
    return result::invalid_argument;
  }
  try {
    if (batch.chunk_count == 1U) {
      pending_chunks_.clear();
      pending_stamp_ = {};
      return apply_complete(batch);
    }
    if (pending_stamp_.session_id != batch.stamp.session_id ||
        pending_stamp_.sequence != batch.stamp.sequence ||
        pending_chunks_.size() != batch.chunk_count || pending_is_full_ != batch.is_full) {
      pending_stamp_ = batch.stamp;
      pending_is_full_ = batch.is_full;
      pending_chunks_.assign(batch.chunk_count, std::nullopt);
    }
    auto& slot = pending_chunks_[batch.chunk_index];
    if (slot.has_value()) {
      return result::success;
    }
    slot = batch;
    if (!std::ranges::all_of(pending_chunks_,
                             [](const auto& chunk) { return chunk.has_value(); })) {
      return result::success;
    }
    ipc_inspection_batch complete;
    complete.stamp = pending_stamp_;
    complete.is_full = pending_is_full_;
    for (auto& chunk : pending_chunks_) {
      complete.changes.insert(complete.changes.end(),
                              std::make_move_iterator(chunk->changes.begin()),
                              std::make_move_iterator(chunk->changes.end()));
    }
    pending_chunks_.clear();
    pending_stamp_ = {};
    return apply_complete(complete);
  } catch (const std::bad_alloc&) {
    pending_chunks_.clear();
    needs_full_snapshot_ = true;
    return result::out_of_memory;
  } catch (...) {
    pending_chunks_.clear();
    needs_full_snapshot_ = true;
    return result::internal;
  }
}

result runtime_scene_mirror::apply_complete(const ipc_inspection_batch& batch) noexcept {
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
  invalidate();
}

void runtime_scene_mirror::invalidate() noexcept {
  needs_full_snapshot_ = true;
  pending_stamp_ = {};
  pending_is_full_ = false;
  pending_chunks_.clear();
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
