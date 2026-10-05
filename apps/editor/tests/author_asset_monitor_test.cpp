// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "author_asset_monitor.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

[[nodiscard]] bool write_text(const std::filesystem::path& path, std::string_view text) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream << text;
  return static_cast<bool>(stream);
}

} // namespace

int main() {
  const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto root = std::filesystem::temp_directory_path() /
                    ("gneiss-author-asset-monitor-" + std::to_string(suffix));
  const auto scenes = root / "scenes";
  const auto scene = scenes / "main.scene.json";
  std::filesystem::create_directories(scenes);
  if (!write_text(scene, "first")) {
    return 1;
  }

  gneiss::editor::author_asset_monitor monitor;
  if (monitor.initialize(root) != gneiss::result::success ||
      monitor.observe("scenes/main.scene.json", false).state !=
          gneiss::editor::author_asset_change_state::idle) {
    return 2;
  }
  if (!write_text(scene, "second") || monitor.observe("scenes/main.scene.json", true).state !=
                                          gneiss::editor::author_asset_change_state::conflict) {
    return 3;
  }
  if (monitor.observe("scenes/main.scene.json", false).state !=
      gneiss::editor::author_asset_change_state::changed) {
    return 4;
  }
  monitor.mark_applied("asset://scenes/main.scene.json");
  if (monitor.status().state != gneiss::editor::author_asset_change_state::applied ||
      monitor.observe("scenes/main.scene.json", false).state !=
          gneiss::editor::author_asset_change_state::idle) {
    return 5;
  }
  if (monitor.observe("textures/stone.png", false).state !=
      gneiss::editor::author_asset_change_state::idle) {
    return 6;
  }
  std::filesystem::remove(scene);
  const auto removed = monitor.observe("scenes/main.scene.json", false);
  if (removed.state != gneiss::editor::author_asset_change_state::changed ||
      removed.operation != gneiss::result::not_found) {
    return 7;
  }
  monitor.mark_failed(removed.uri, removed.operation);
  if (monitor.status().state != gneiss::editor::author_asset_change_state::failed) {
    return 8;
  }
  // 丢事件后同时找回删除和新增；每次最多枚举一个目录项或已知路径。
  const auto prefab = root / "new.prefab.json";
  if (!write_text(prefab, "new prefab")) {
    return 9;
  }
  monitor.request_rescan();
  bool found_removed = false;
  bool found_new = false;
  for (int frame = 0; frame < 32 && monitor.is_rescanning(); ++frame) {
    std::vector<std::filesystem::path> paths;
    if (monitor.poll_rescan(paths, 1U) != gneiss::result::success || paths.size() > 1U) {
      return 10;
    }
    for (const auto& path : paths) {
      const auto change = monitor.observe(path, false);
      found_removed |= change.uri == "asset://scenes/main.scene.json" &&
                       change.operation == gneiss::result::not_found;
      found_new |= change.uri == "asset://new.prefab.json" &&
                   change.state == gneiss::editor::author_asset_change_state::changed;
    }
  }
  if (monitor.is_rescanning() || !found_removed || !found_new || !write_text(scene, "third")) {
    return 11;
  }
  monitor.request_rescan();
  bool found_conflict = false;
  for (int frame = 0; frame < 32 && monitor.is_rescanning(); ++frame) {
    std::vector<std::filesystem::path> paths;
    (void)monitor.poll_rescan(paths, 1U);
    for (const auto& path : paths) {
      found_conflict |=
          monitor.observe(path, true).state == gneiss::editor::author_asset_change_state::conflict;
    }
  }
  if (!found_conflict || monitor.observe("scenes/main.scene.json", false).state !=
                             gneiss::editor::author_asset_change_state::changed) {
    return 12;
  }
  std::filesystem::remove_all(root);
  monitor.request_rescan();
  std::vector<std::filesystem::path> paths;
  if (monitor.poll_rescan(paths) != gneiss::result::io || monitor.is_rescanning()) {
    return 13;
  }
  std::filesystem::create_directories(root);
  monitor.request_rescan();
  if (monitor.poll_rescan(paths) != gneiss::result::success) {
    return 14;
  }
  std::filesystem::remove_all(root);
  return 0;
}
