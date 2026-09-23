// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "asset_reimport_queue.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace {

using queue = gneiss::editor::asset_reimport_queue;

[[nodiscard]] bool has_state(const std::vector<gneiss::editor::asset_reimport_event>& events,
                             gneiss::editor::asset_reimport_state state) {
  return std::ranges::any_of(events, [state](const auto& event) { return event.state == state; });
}

} // namespace

int main() { // NOLINT(bugprone-exception-escape)
  const auto fixture_root = std::filesystem::path{GNEISS_EDITOR_TEST_GLTF_ROOT};
  const auto root = std::filesystem::temp_directory_path() / "gneiss-reimport-queue-test";
  const auto assets = root / "assets";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(assets);

  const auto initial =
      gneiss::editor::import_external_asset(root, assets, fixture_root / "static_triangle.gltf");
  if (initial.result != gneiss::editor::editor_import_result::success) {
    std::filesystem::remove_all(root);
    return 1;
  }
  const auto relative = initial.source_path.lexically_relative(root / "sources");
  const auto start = queue::clock::now();
  queue reimports({.debounce = std::chrono::milliseconds{20},
                   .stable_read_delay = std::chrono::milliseconds{10},
                   .capacity = 2U});
  if (reimports.notify({}, start) != gneiss::result::invalid_argument ||
      reimports.notify(relative, start) != gneiss::result::success ||
      reimports.notify(relative, start + std::chrono::milliseconds{5}) != gneiss::result::success ||
      reimports.pending_count() != 1U ||
      reimports.tick(root, assets, start + std::chrono::milliseconds{24}) != 0U ||
      reimports.tick(root, assets, start + std::chrono::milliseconds{25}) != 0U ||
      reimports.tick(root, assets, start + std::chrono::milliseconds{35}) != 0U) {
    std::filesystem::remove_all(root);
    return 2;
  }
  std::vector<gneiss::editor::asset_reimport_event> events;
  const auto unchanged_event_count = reimports.poll_events(events);
  if (unchanged_event_count == 0U ||
      !has_state(events, gneiss::editor::asset_reimport_state::unchanged)) {
    std::filesystem::remove_all(root);
    return 3;
  }

  std::ofstream(initial.source_path, std::ios::binary | std::ios::app) << ' ';
  const auto changed = start + std::chrono::seconds{1};
  if (reimports.notify(relative, changed) != gneiss::result::success ||
      reimports.tick(root, assets, changed + std::chrono::milliseconds{20}) != 0U ||
      reimports.tick(root, assets, changed + std::chrono::milliseconds{30}) != 1U) {
    std::filesystem::remove_all(root);
    return 4;
  }
  events.clear();
  const auto imported_event_count = reimports.poll_events(events);
  if (imported_event_count == 0U ||
      !has_state(events, gneiss::editor::asset_reimport_state::importing) ||
      !has_state(events, gneiss::editor::asset_reimport_state::succeeded)) {
    std::filesystem::remove_all(root);
    return 5;
  }

  const auto untracked = std::filesystem::path{"untracked.gltf"};
  std::filesystem::copy_file(fixture_root / "static_triangle.gltf", root / "sources" / untracked);
  const auto later = changed + std::chrono::seconds{1};
  if (reimports.notify(untracked, later) != gneiss::result::success ||
      reimports.tick(root, assets, later + std::chrono::milliseconds{20}) != 0U) {
    std::filesystem::remove_all(root);
    return 6;
  }
  events.clear();
  const auto untracked_event_count = reimports.poll_events(events);
  if (untracked_event_count == 0U ||
      !has_state(events, gneiss::editor::asset_reimport_state::untracked)) {
    std::filesystem::remove_all(root);
    return 7;
  }

  queue bounded({.capacity = 1U});
  if (bounded.notify("first.gltf", start) != gneiss::result::success ||
      bounded.notify("second.gltf", start) != gneiss::result::not_ready ||
      bounded.dropped_candidate_count() != 1U) {
    std::filesystem::remove_all(root);
    return 8;
  }

  const auto second =
      gneiss::editor::import_external_asset(root, assets, fixture_root / "static_triangle.gltf");
  if (second.result != gneiss::editor::editor_import_result::success) {
    return 11;
  }
  const auto failing_relative = second.source_path.lexically_relative(root / "sources");
  std::ofstream(second.source_path, std::ios::app) << ' ';
  std::ofstream(initial.source_path, std::ios::app) << ' ';
  int failure_mode = 1;
  std::size_t attempts = 0U;
  queue isolated({.debounce = std::chrono::milliseconds{0},
                  .stable_read_delay = std::chrono::milliseconds{1},
                  .capacity = 16U},
                 [&](const auto& project, const auto& asset_root, const auto& source) {
                   ++attempts;
                   if (source == second.source_path && failure_mode == 1) {
                     throw std::runtime_error("test importer failure");
                   }
                   if (failure_mode == 2) {
                     throw 42;
                   }
                   return gneiss::editor::reimport_source_asset(project, asset_root, source);
                 });
  // 带后缀的第二个源按名称先处理，用来确认失败不会挡住随后健康的候选。
  if (failing_relative >= relative ||
      isolated.notify(failing_relative, start) != gneiss::result::success ||
      isolated.notify(relative, start) != gneiss::result::success ||
      isolated.tick(root, assets, start) != 0U ||
      isolated.tick(root, assets, start + std::chrono::milliseconds{1}) != 1U || attempts != 1U ||
      isolated.pending_count() != 1U) {
    return 12;
  }
  events.clear();
  (void)isolated.poll_events(events);
  if (!std::ranges::any_of(events,
                           [&](const auto& event) {
                             return event.state == gneiss::editor::asset_reimport_state::failed &&
                                    event.relative_path == failing_relative &&
                                    event.import.source_path == second.source_path &&
                                    event.import.diagnostic.find("test importer failure") !=
                                        std::string::npos;
                           }) ||
      isolated.tick(root, assets, start + std::chrono::milliseconds{2}) != 1U || attempts != 2U ||
      isolated.pending_count() != 0U ||
      isolated.tick(root, assets, start + std::chrono::milliseconds{3}) != 0U || attempts != 2U) {
    return 13;
  }
  failure_mode = 0;
  if (isolated.notify(failing_relative, later) != gneiss::result::success ||
      isolated.tick(root, assets, later) != 0U ||
      isolated.tick(root, assets, later + std::chrono::milliseconds{1}) != 1U || attempts != 3U ||
      isolated.pending_count() != 0U) {
    return 14;
  }
  events.clear();
  (void)isolated.poll_events(events);
  if (!std::ranges::any_of(events, [&](const auto& event) {
        return event.state == gneiss::editor::asset_reimport_state::succeeded &&
               event.relative_path == failing_relative;
      })) {
    return 15;
  }
  failure_mode = 2;
  std::ofstream(second.source_path, std::ios::app) << ' ';
  if (isolated.notify(failing_relative, later) != gneiss::result::success ||
      isolated.tick(root, assets, later) != 0U ||
      isolated.tick(root, assets, later + std::chrono::milliseconds{1}) != 1U ||
      isolated.pending_count() != 0U) {
    return 16;
  }
  events.clear();
  (void)isolated.poll_events(events);
  if (!has_state(events, gneiss::editor::asset_reimport_state::failed)) {
    return 17;
  }

  std::filesystem::remove(initial.source_path);
  const auto removed = later + std::chrono::seconds{1};
  if (reimports.notify(relative, removed) != gneiss::result::success ||
      reimports.tick(root, assets, removed + std::chrono::milliseconds{20}) != 0U) {
    std::filesystem::remove_all(root);
    return 9;
  }
  events.clear();
  const auto removed_event_count = reimports.poll_events(events);
  if (removed_event_count == 0U ||
      !has_state(events, gneiss::editor::asset_reimport_state::removed)) {
    std::filesystem::remove_all(root);
    return 10;
  }

  std::filesystem::remove_all(root);
  return 0;
}
