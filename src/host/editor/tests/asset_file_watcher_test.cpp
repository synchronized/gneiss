// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "asset_file_watcher.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;

bool wait_for(gneiss::editor::asset_file_watcher& watcher, std::string_view path,
              gneiss::editor::asset_file_event_kind kind) {
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline) {
    std::vector<gneiss::editor::asset_file_event> events;
    (void)watcher.poll_events(events);
    if (std::ranges::any_of(events, [&](const auto& event) {
          return event.relative_path.generic_string() == path && event.kind == kind;
        })) {
      return true;
    }
    std::this_thread::sleep_for(10ms);
  }
  return false;
}

bool wait_for_rename(gneiss::editor::asset_file_watcher& watcher) {
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline) {
    std::vector<gneiss::editor::asset_file_event> events;
    (void)watcher.poll_events(events);
    if (std::ranges::any_of(events, [](const auto& event) {
          const auto path = event.relative_path.generic_string();
          return event.kind == gneiss::editor::asset_file_event_kind::renamed &&
                 (path == "nested/asset.txt" || path == "nested/renamed.txt");
        })) {
      return true;
    }
    std::this_thread::sleep_for(10ms);
  }
  return false;
}

bool wait_for_candidate(gneiss::editor::asset_file_watcher& watcher, std::string_view path) {
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline) {
    std::vector<gneiss::editor::asset_file_event> events;
    (void)watcher.poll_events(events);
    if (std::ranges::any_of(events, [&](const auto& event) {
          return event.relative_path.generic_string() == path &&
                 event.operation == gneiss::result::success;
        })) {
      return true;
    }
    std::this_thread::sleep_for(10ms);
  }
  return false;
}

void write_text(const std::filesystem::path& path, std::string_view text) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output << text;
}

} // namespace

int main() {
  const auto root = std::filesystem::temp_directory_path() / "gneiss-asset-file-watcher-test";
  std::error_code error;
  std::filesystem::remove_all(root, error);
  std::filesystem::create_directories(root / "nested");
  write_text(root / "nested" / "asset.txt", "initial");

  // 无源资产的工程无需创建空目录；目录后来出现后，同一监听器可以正常启动。
  gneiss::editor::asset_file_watcher optional;
  const auto sources = root / "sources";
  if (optional.start(sources, true) != gneiss::result::not_ready || optional.is_running() ||
      std::filesystem::exists(sources) || optional.stop() != gneiss::result::not_ready ||
      optional.start(sources, true) != gneiss::result::not_ready) {
    return 8;
  }
  write_text(sources, "not a directory");
  if (optional.start(sources, true) != gneiss::result::invalid_argument || optional.is_running()) {
    return 9;
  }
  std::filesystem::remove(sources);
  std::filesystem::create_directory(sources);
  if (optional.start(sources, true) != gneiss::result::success || !optional.is_running()) {
    return 10;
  }
  write_text(sources / "late.txt", "created after startup");
  if (!wait_for_candidate(optional, "late.txt") || optional.stop() != gneiss::result::success) {
    return 11;
  }

  gneiss::editor::asset_file_watcher watcher{2U};
  if (watcher.start(root / "missing") != gneiss::result::not_found ||
      watcher.start(root) != gneiss::result::success || !watcher.is_running()) {
    return 1;
  }

  write_text(root / "nested" / "asset.txt", "changed");
  if (!wait_for(watcher, "nested/asset.txt", gneiss::editor::asset_file_event_kind::changed)) {
    return 2;
  }

  std::filesystem::rename(root / "nested" / "asset.txt", root / "nested" / "renamed.txt");
  if (!wait_for_rename(watcher)) {
    return 3;
  }

  std::filesystem::create_directory(root / "dynamic");
  if (!wait_for(watcher, "dynamic", gneiss::editor::asset_file_event_kind::renamed)) {
    return 4;
  }
  std::this_thread::sleep_for(50ms);
  write_text(root / "dynamic" / "new.txt", "new");
  if (!wait_for_candidate(watcher, "dynamic/new.txt")) {
    return 5;
  }

  for (int index = 0; index < 16; ++index) {
    write_text(root / ("event-" + std::to_string(index) + ".txt"), "event");
  }
  std::this_thread::sleep_for(100ms);
  std::vector<gneiss::editor::asset_file_event> limited;
  if (watcher.poll_events(limited, 1U) > 1U || watcher.dropped_event_count() == 0U) {
    return 6;
  }

  if (watcher.stop() != gneiss::result::success || watcher.is_running() ||
      watcher.stop() != gneiss::result::not_ready) {
    return 7;
  }
  // 重启到另一目录不得带入旧事件，但历史丢失计数不能被重启掩盖。
  const auto dropped_before_restart = watcher.dropped_event_count();
  if (watcher.start(sources) != gneiss::result::success) {
    return 12;
  }
  std::vector<gneiss::editor::asset_file_event> restarted_events;
  if (watcher.poll_events(restarted_events) != 0U ||
      watcher.dropped_event_count() != dropped_before_restart) {
    return 13;
  }
  write_text(sources / "restarted.txt", "new watch session");
  if (!wait_for_candidate(watcher, "restarted.txt") || watcher.stop() != gneiss::result::success) {
    return 14;
  }
  std::filesystem::remove_all(root, error);
  return 0;
}
