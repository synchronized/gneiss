// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_scene_mirror.hpp"

#include <limits>

namespace {

gneiss::editor::runtime_scene_change upsert(std::uint64_t value, std::uint32_t generation,
                                            std::uint64_t parent, std::string uuid) {
  gneiss::editor::runtime_scene_change change;
  change.id = {value, generation};
  change.node.id = change.id;
  change.node.parent = parent == 0U ? gneiss::editor::runtime_object_id{}
                                    : gneiss::editor::runtime_object_id{parent, 1U};
  change.node.uuid = std::move(uuid);
  change.node.name = change.node.uuid;
  return change;
}

bool test_apply_and_resync() {
  gneiss::editor::runtime_scene_mirror mirror;
  gneiss::editor::runtime_scene_batch full{
      .stamp = {9U, 4U},
      .is_full = true,
      .changes = {upsert(1U, 1U, 0U, "root"), upsert(2U, 1U, 1U, "child")}};
  if (mirror.apply(full) != gneiss::result::success || mirror.session_id() != 9U ||
      mirror.needs_full_snapshot() || mirror.nodes().size() != 2U) {
    return false;
  }
  auto rename = upsert(2U, 1U, 1U, "child");
  rename.node.name = "Renamed";
  gneiss::editor::runtime_scene_batch delta{.stamp = {9U, 5U}, .changes = {rename}};
  if (mirror.apply(delta) != gneiss::result::success || mirror.nodes()[1].name != "Renamed" ||
      mirror.apply(delta) != gneiss::result::success) {
    return false;
  }
  delta.stamp.sequence = 7U;
  if (mirror.apply(delta) != gneiss::result::not_ready || !mirror.needs_full_snapshot()) {
    return false;
  }
  full.stamp = {10U, 20U};
  full.changes = {upsert(3U, 1U, 0U, "new-root")};
  return mirror.apply(full) == gneiss::result::success && mirror.session_id() == 10U &&
         mirror.nodes().size() == 1U && mirror.nodes()[0].uuid == "new-root";
}

bool test_invalid_graph_is_atomic() {
  gneiss::editor::runtime_scene_mirror mirror;
  gneiss::editor::runtime_scene_batch full{
      .stamp = {1U, 1U}, .is_full = true, .changes = {upsert(1U, 1U, 0U, "root")}};
  if (mirror.apply(full) != gneiss::result::success) {
    return false;
  }
  auto orphan = upsert(2U, 1U, 99U, "orphan");
  gneiss::editor::runtime_scene_batch delta{.stamp = {1U, 2U}, .changes = {orphan}};
  return mirror.apply(delta) == gneiss::result::invalid_argument && mirror.needs_full_snapshot() &&
         mirror.nodes().size() == 1U && mirror.nodes()[0].uuid == "root";
}

bool test_invalidate_preserves_visible_snapshot() {
  gneiss::editor::runtime_scene_mirror mirror;
  const gneiss::editor::runtime_scene_batch full{
      .stamp = {3U, 1U}, .is_full = true, .changes = {upsert(1U, 1U, 0U, "root")}};
  if (mirror.apply(full) != gneiss::result::success) {
    return false;
  }
  mirror.invalidate();
  return mirror.needs_full_snapshot() && mirror.nodes().size() == 1U &&
         mirror.nodes().front().uuid == "root";
}

bool test_chunked_snapshot_is_atomic() {
  gneiss::editor::runtime_scene_mirror mirror;
  gneiss::editor::runtime_scene_batch second{.stamp = {5U, 1U},
                                             .is_full = true,
                                             .chunk_index = 1U,
                                             .chunk_count = 2U,
                                             .changes = {upsert(2U, 1U, 1U, "child")}};
  if (mirror.apply(second) != gneiss::result::success || !mirror.nodes().empty() ||
      !mirror.needs_full_snapshot()) {
    return false;
  }
  gneiss::editor::runtime_scene_batch first{.stamp = {5U, 1U},
                                            .is_full = true,
                                            .chunk_index = 0U,
                                            .chunk_count = 2U,
                                            .changes = {upsert(1U, 1U, 0U, "root")}};
  if (mirror.apply(first) != gneiss::result::success || mirror.nodes().size() != 2U ||
      mirror.needs_full_snapshot()) {
    return false;
  }
  return mirror.apply(first) == gneiss::result::success && mirror.nodes().size() == 2U;
}

bool test_prefab_sources_use_composite_identity() {
  auto first = upsert(1U, 1U, 0U, "source");
  first.node.prefab_instance_uuid = "instance-a";
  first.node.prefab_source_node_uuid = "source";
  auto second = upsert(2U, 1U, 0U, "source");
  second.node.prefab_instance_uuid = "instance-b";
  second.node.prefab_source_node_uuid = "source";
  gneiss::editor::runtime_scene_mirror mirror;
  const gneiss::editor::runtime_scene_batch full{
      .stamp = {6U, 1U}, .is_full = true, .changes = {first, second}};
  return mirror.apply(full) == gneiss::result::success && mirror.nodes().size() == 2U;
}

[[nodiscard]] bool test_inspection_sequence_tracker() {
  gneiss::editor::runtime_scene_sequence_tracker tracker;
  if (tracker.begin(7U, 2U) != gneiss::result::success ||
      tracker.observe({7U, 2U}) != gneiss::editor::runtime_scene_sequence_result::accepted ||
      tracker.observe({7U, 2U}) != gneiss::editor::runtime_scene_sequence_result::duplicate ||
      tracker.observe({7U, 4U}) != gneiss::editor::runtime_scene_sequence_result::gap ||
      tracker.observe({8U, 3U}) != gneiss::editor::runtime_scene_sequence_result::stale_session ||
      tracker.observe({7U, 3U}) != gneiss::editor::runtime_scene_sequence_result::accepted) {
    return false;
  }
  tracker.reset();
  if (tracker.observe({7U, 4U}) != gneiss::editor::runtime_scene_sequence_result::invalid ||
      tracker.begin(0U) != gneiss::result::invalid_argument ||
      tracker.begin(7U, 0U) != gneiss::result::invalid_argument) {
    return false;
  }
  const auto maximum = std::numeric_limits<std::uint64_t>::max();
  return tracker.begin(7U, maximum) == gneiss::result::success &&
         tracker.observe({7U, maximum}) ==
             gneiss::editor::runtime_scene_sequence_result::accepted &&
         tracker.session_id() == 0U &&
         tracker.observe({7U, 1U}) == gneiss::editor::runtime_scene_sequence_result::invalid;
}

} // namespace

int main() {
  return test_inspection_sequence_tracker() && test_apply_and_resync() &&
                 test_invalid_graph_is_atomic() && test_invalidate_preserves_visible_snapshot() &&
                 test_chunked_snapshot_is_atomic() && test_prefab_sources_use_composite_identity()
             ? 0
             : 1;
}
