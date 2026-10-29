// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/value_result.hpp"
#include "engine/core/error.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/world/system_scheduler.hpp"
#include "engine/function/world/world_state.hpp"

#include <array>
#include <cstddef>
#include <new>

namespace {

struct position {
  int value;
};

struct system_context {
  std::array<int, 4> events{};
  std::size_t count = 0;
  int delta = 0;
};

gneiss::engine::error update_position(gneiss::engine::world_state& world,
                                      void* user_data) noexcept {
  auto& context = *static_cast<system_context*>(user_data);
  context.events[context.count++] = 1;
  try {
    world.each<position>([&context](position& value) { value.value += context.delta; });
    return gneiss::engine::error::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::error::out_of_memory;
  } catch (...) {
    return gneiss::engine::error::internal;
  }
}

gneiss::engine::error fail_system(gneiss::engine::world_state& /*world*/,
                                  void* user_data) noexcept {
  auto& context = *static_cast<system_context*>(user_data);
  context.events[context.count++] = 2;
  return gneiss::engine::error::invalid_state;
}

gneiss::engine::error skipped_system(gneiss::engine::world_state& /*world*/,
                                     void* user_data) noexcept {
  auto& context = *static_cast<system_context*>(user_data);
  context.events[context.count++] = 3;
  return gneiss::engine::error::success;
}

} // namespace

int run_tests() {
  gneiss::engine::world_state first{1};
  gneiss::engine::world_state second{2};
  const auto entity = first.create_entity();
  first.emplace<position>(entity, 10);
  if (!first.is_alive(entity) || second.is_alive(entity) || first.entity_count() != 1U) {
    return 1;
  }

  system_context context{.delta = 5};
  gneiss::engine::system_scheduler scheduler;
  if (scheduler.add(1, update_position, &context) != gneiss::engine::error::success ||
      scheduler.add(2, fail_system, &context) != gneiss::engine::error::success ||
      scheduler.add(3, skipped_system, &context) != gneiss::engine::error::success ||
      scheduler.add(3, skipped_system, &context) != gneiss::engine::error::invalid_argument ||
      scheduler.run(first) != gneiss::engine::error::invalid_state) {
    return 2;
  }
  const auto* value = first.get<position>(entity);
  if (value == nullptr || value->value != 15 || context.count != 2U || context.events[0] != 1 ||
      context.events[1] != 2) {
    return 3;
  }
  if (!first.destroy_entity(entity) || first.is_alive(entity) ||
      first.get<position>(entity) != nullptr) {
    return 4;
  }
  const auto replacement = first.create_entity();
  if (replacement == entity || first.is_alive(entity) || !first.is_alive(replacement)) {
    return 5;
  }
  const auto visible = first.create_entity();
  first.emplace<position>(visible, 40);
  int visited = 0;
  first.each<position>([&](const position&) { ++visited; });
  if (!first.is_alive(visible) || first.entity_count() != 2U || visited != 1)
    return 6;
  gneiss::scene_node_id parent{}, child{};
  auto& tree = first.scene();
  if (gneiss::test_support::assign_value(tree.create({}, replacement), parent).failed() ||
      gneiss::test_support::assign_value(tree.create(parent, visible), child).failed() ||
      tree.get_entity(child) != visible || tree.destroy(parent).failed() ||
      tree.get_local(child) != nullptr || !first.destroy_entity(visible) ||
      first.destroy_entity(visible) || first.entity_count() != 1U)
    return 7;
  return 0;
}

int main() {
  try {
    return run_tests();
  } catch (...) {
    return 99;
  }
}
