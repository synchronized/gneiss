// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/error.hpp"
#include "engine/core/guid.hpp"
#include "runtime_scene_mirror.hpp"

#include <limits>

namespace {

gneiss::editor::runtime_scene_change upsert(std::uint64_t value, std::uint32_t generation,
                                            std::uint64_t parent, gneiss::engine::guid uuid) {
  gneiss::editor::runtime_scene_change change;
  change.id = {value, generation};
  change.node.id = change.id;
  change.node.parent = parent == 0U ? gneiss::editor::runtime_object_id{}
                                    : gneiss::editor::runtime_object_id{parent, 1U};
  change.node.uuid = std::move(uuid);
  change.node.name = change.node.uuid.string();
  return change;
}

bool test_apply_and_resync() {
  gneiss::editor::runtime_scene_mirror mirror;
  gneiss::editor::runtime_scene_batch full{
      .stamp = {9U, 4U},
      .is_full = true,
      .changes = {
          upsert(1U, 1U, 0U, *gneiss::engine::guid::parse("21470280-22a3-5087-9443-62d27dfc4bbf")),
          upsert(2U, 1U, 1U,
                 *gneiss::engine::guid::parse("0552e973-b75c-5936-a81b-c56bcd7688d5"))}};
  if (mirror.apply(full) != gneiss::engine::error::success || mirror.session_id() != 9U ||
      mirror.needs_full_snapshot() || mirror.nodes().size() != 2U) {
    return false;
  }
  auto rename =
      upsert(2U, 1U, 1U, *gneiss::engine::guid::parse("0552e973-b75c-5936-a81b-c56bcd7688d5"));
  rename.node.name = "Renamed";
  gneiss::editor::runtime_scene_batch delta{.stamp = {9U, 5U}, .changes = {rename}};
  if (mirror.apply(delta) != gneiss::engine::error::success ||
      mirror.nodes()[1].name != "Renamed" ||
      mirror.apply(delta) != gneiss::engine::error::success) {
    return false;
  }
  delta.stamp.sequence = 7U;
  if (mirror.apply(delta) != gneiss::engine::error::not_ready || !mirror.needs_full_snapshot()) {
    return false;
  }
  full.stamp = {10U, 20U};
  full.changes = {
      upsert(3U, 1U, 0U, *gneiss::engine::guid::parse("57f469b5-1072-5dc5-aadd-bd86ed451e8c"))};
  return mirror.apply(full) == gneiss::engine::error::success && mirror.session_id() == 10U &&
         mirror.nodes().size() == 1U &&
         mirror.nodes()[0].uuid ==
             *gneiss::engine::guid::parse("57f469b5-1072-5dc5-aadd-bd86ed451e8c");
}

bool test_invalid_graph_is_atomic() {
  gneiss::editor::runtime_scene_mirror mirror;
  gneiss::editor::runtime_scene_batch full{
      .stamp = {1U, 1U},
      .is_full = true,
      .changes = {upsert(1U, 1U, 0U,
                         *gneiss::engine::guid::parse("21470280-22a3-5087-9443-62d27dfc4bbf"))}};
  if (mirror.apply(full) != gneiss::engine::error::success) {
    return false;
  }
  auto orphan =
      upsert(2U, 1U, 99U, *gneiss::engine::guid::parse("96eadd94-e998-5d37-9d62-c408d137eb09"));
  gneiss::editor::runtime_scene_batch delta{.stamp = {1U, 2U}, .changes = {orphan}};
  return mirror.apply(delta) == gneiss::engine::error::invalid_argument &&
         mirror.needs_full_snapshot() && mirror.nodes().size() == 1U &&
         mirror.nodes()[0].uuid ==
             *gneiss::engine::guid::parse("21470280-22a3-5087-9443-62d27dfc4bbf");
}

bool test_invalidate_preserves_visible_snapshot() {
  gneiss::editor::runtime_scene_mirror mirror;
  const gneiss::editor::runtime_scene_batch full{
      .stamp = {3U, 1U},
      .is_full = true,
      .changes = {upsert(1U, 1U, 0U,
                         *gneiss::engine::guid::parse("21470280-22a3-5087-9443-62d27dfc4bbf"))}};
  if (mirror.apply(full) != gneiss::engine::error::success) {
    return false;
  }
  mirror.invalidate();
  return mirror.needs_full_snapshot() && mirror.nodes().size() == 1U &&
         mirror.nodes().front().uuid ==
             *gneiss::engine::guid::parse("21470280-22a3-5087-9443-62d27dfc4bbf");
}

bool test_chunked_snapshot_is_atomic() {
  gneiss::editor::runtime_scene_mirror mirror;
  gneiss::editor::runtime_scene_batch second{
      .stamp = {5U, 1U},
      .is_full = true,
      .chunk_index = 1U,
      .chunk_count = 2U,
      .changes = {upsert(2U, 1U, 1U,
                         *gneiss::engine::guid::parse("0552e973-b75c-5936-a81b-c56bcd7688d5"))}};
  if (mirror.apply(second) != gneiss::engine::error::success || !mirror.nodes().empty() ||
      !mirror.needs_full_snapshot()) {
    return false;
  }
  gneiss::editor::runtime_scene_batch first{
      .stamp = {5U, 1U},
      .is_full = true,
      .chunk_index = 0U,
      .chunk_count = 2U,
      .changes = {upsert(1U, 1U, 0U,
                         *gneiss::engine::guid::parse("21470280-22a3-5087-9443-62d27dfc4bbf"))}};
  if (mirror.apply(first) != gneiss::engine::error::success || mirror.nodes().size() != 2U ||
      mirror.needs_full_snapshot()) {
    return false;
  }
  return mirror.apply(first) == gneiss::engine::error::success && mirror.nodes().size() == 2U;
}

bool test_prefab_sources_use_composite_identity() {
  auto first =
      upsert(1U, 1U, 0U, *gneiss::engine::guid::parse("117a1bb4-e8b7-5da7-b737-9d7cf00fff3c"));
  first.node.prefab_instance_uuid =
      *gneiss::engine::guid::parse("6693b403-d7fb-5b73-ab3a-da0bbe718045");
  first.node.prefab_source_node_uuid =
      *gneiss::engine::guid::parse("117a1bb4-e8b7-5da7-b737-9d7cf00fff3c");
  auto second =
      upsert(2U, 1U, 0U, *gneiss::engine::guid::parse("117a1bb4-e8b7-5da7-b737-9d7cf00fff3c"));
  second.node.prefab_instance_uuid =
      *gneiss::engine::guid::parse("940a392c-a4eb-55a6-a13d-682e4ff6290b");
  second.node.prefab_source_node_uuid =
      *gneiss::engine::guid::parse("117a1bb4-e8b7-5da7-b737-9d7cf00fff3c");
  gneiss::editor::runtime_scene_mirror mirror;
  const gneiss::editor::runtime_scene_batch full{
      .stamp = {6U, 1U}, .is_full = true, .changes = {first, second}};
  return mirror.apply(full) == gneiss::engine::error::success && mirror.nodes().size() == 2U;
}

[[nodiscard]] bool test_inspection_sequence_tracker() {
  gneiss::editor::runtime_scene_sequence_tracker tracker;
  if (tracker.begin(7U, 2U) != gneiss::engine::error::success ||
      tracker.observe({7U, 2U}) != gneiss::editor::runtime_scene_sequence_result::accepted ||
      tracker.observe({7U, 2U}) != gneiss::editor::runtime_scene_sequence_result::duplicate ||
      tracker.observe({7U, 4U}) != gneiss::editor::runtime_scene_sequence_result::gap ||
      tracker.observe({8U, 3U}) != gneiss::editor::runtime_scene_sequence_result::stale_session ||
      tracker.observe({7U, 3U}) != gneiss::editor::runtime_scene_sequence_result::accepted) {
    return false;
  }
  tracker.reset();
  if (tracker.observe({7U, 4U}) != gneiss::editor::runtime_scene_sequence_result::invalid ||
      tracker.begin(0U) != gneiss::engine::error::invalid_argument ||
      tracker.begin(7U, 0U) != gneiss::engine::error::invalid_argument) {
    return false;
  }
  const auto maximum = std::numeric_limits<std::uint64_t>::max();
  return tracker.begin(7U, maximum) == gneiss::engine::error::success &&
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
