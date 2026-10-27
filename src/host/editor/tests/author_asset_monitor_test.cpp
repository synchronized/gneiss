// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "author_asset_monitor.hpp"
#include <algorithm>
int main() {
  using namespace gneiss::editor;
  using gneiss::engine::result;
  author_asset_monitor monitor;
  constexpr auto uri = "asset://scenes/main.scene.json";
  monitor.establish_baseline(uri, 1);
  if (monitor.observe("scenes/main.scene.json", false, result::success, 1).state !=
      author_asset_change_state::idle)
    return 1;
  if (monitor.observe("scenes/main.scene.json", true, result::success, 2).state !=
      author_asset_change_state::conflict)
    return 2;
  if (monitor.observe("scenes/main.scene.json", false, result::success, 2).state !=
      author_asset_change_state::changed)
    return 3;
  monitor.mark_applied(uri, 2);
  monitor.establish_baseline(uri, 1);
  if (monitor.observe("scenes/main.scene.json", false, result::success, 2).state !=
      author_asset_change_state::idle)
    return 4;
  const auto deleted = monitor.observe("scenes/main.scene.json", false, result::not_found, 0);
  if (deleted.operation != result::not_found || deleted.state != author_asset_change_state::changed)
    return 5;
  const auto known = monitor.known_uris();
  if (known.size() != 1 || known.front() != uri)
    return 6;
  monitor.mark_failed(uri, result::io);
  if (monitor.status().state != author_asset_change_state::failed)
    return 7;
  return monitor.observe("textures/a.png", false, result::success, 1).state ==
                 author_asset_change_state::idle
             ? 0
             : 8;
}
