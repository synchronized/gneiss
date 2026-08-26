// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/gneiss.hpp>

#include <utility>

int main() {
  gneiss::world first;
  if (gneiss::world::create(first) != gneiss::result::success) {
    return 1;
  }
  gneiss::entity_id entity;
  if (first.create_entity(entity) != gneiss::result::success || !entity.is_valid()) {
    return 2;
  }
  bool is_alive = false;
  if (first.is_alive(entity, is_alive) != gneiss::result::success || !is_alive) {
    return 3;
  }

  gneiss::world second{std::move(first)};
  // NOLINTNEXTLINE(bugprone-use-after-move): world 明确定义了可查询的移动后状态。
  if (first.is_valid() || !second.is_valid() ||
      second.destroy_entity(entity) != gneiss::result::success) {
    return 4;
  }
  return 0;
}
