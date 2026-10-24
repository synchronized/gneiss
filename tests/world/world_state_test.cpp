// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
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

gneiss::engine::core::result update_position(gneiss::engine::world::world_state& world,
                                             void* user_data) noexcept {
  auto& context = *static_cast<system_context*>(user_data);
  context.events[context.count++] = 1;
  try {
    world.each<position>([&context](position& value) { value.value += context.delta; });
    return gneiss::engine::core::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::core::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::core::result::internal;
  }
}

gneiss::engine::core::result fail_system(gneiss::engine::world::world_state& /*world*/,
                                         void* user_data) noexcept {
  auto& context = *static_cast<system_context*>(user_data);
  context.events[context.count++] = 2;
  return gneiss::engine::core::result::invalid_state;
}

gneiss::engine::core::result skipped_system(gneiss::engine::world::world_state& /*world*/,
                                            void* user_data) noexcept {
  auto& context = *static_cast<system_context*>(user_data);
  context.events[context.count++] = 3;
  return gneiss::engine::core::result::success;
}

} // namespace

int run_tests() {
  gneiss::engine::world::world_state first{1};
  gneiss::engine::world::world_state second{2};
  const auto entity = first.create_entity();
  first.emplace<position>(entity, 10);
  if (!first.is_alive(entity) || second.is_alive(entity) || first.entity_count() != 1U) {
    return 1;
  }

  system_context context{.delta = 5};
  gneiss::engine::world::system_scheduler scheduler;
  if (scheduler.add(1, update_position, &context) != gneiss::engine::core::result::success ||
      scheduler.add(2, fail_system, &context) != gneiss::engine::core::result::success ||
      scheduler.add(3, skipped_system, &context) != gneiss::engine::core::result::success ||
      scheduler.add(3, skipped_system, &context) !=
          gneiss::engine::core::result::invalid_argument ||
      scheduler.run(first) != gneiss::engine::core::result::invalid_state) {
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
  const auto group = first.begin_publication();
  const auto candidate = first.create_staged_entity(group);
  const auto cancelled = first.create_staged_entity(group);
  gneiss::engine::core::result other_thread_result = gneiss::engine::core::result::success;
  std::thread worker([&] { other_thread_result = first.publish(group); });
  worker.join();
  if (other_thread_result != gneiss::engine::core::result::invalid_state) {
    return 10;
  }
  if (candidate == GNEISS_NULL_ENTITY_ID || !first.emplace_staged<position>(group, candidate, 40) ||
      first.is_alive(candidate) || first.get<position>(candidate) != nullptr ||
      first.entity_count() != 1U || first.destroy_entity(candidate) ||
      second.create_staged_entity(group) != GNEISS_NULL_ENTITY_ID ||
      second.publish(group) != gneiss::engine::core::result::invalid_state) {
    return 6;
  }
  int visited = 0;
  first.each<position>([&](const position&) { ++visited; });
  if (visited != 0 || !first.destroy_staged_entity(group, cancelled) ||
      first.destroy_staged_entity(group, cancelled) ||
      first.publish(group) != gneiss::engine::core::result::success || first.entity_count() != 2U ||
      !first.is_alive(candidate) || first.get<position>(candidate)->value != 40 ||
      first.publish(group) != gneiss::engine::core::result::invalid_state ||
      first.emplace_staged<position>(group, candidate, 41) ||
      first.create_staged_entity(group) != GNEISS_NULL_ENTITY_ID) {
    return 7;
  }
  first.each<position>([&](entt::entity, const position&) { ++visited; });
  if (visited != 1 || !first.destroy_entity(candidate) || first.is_alive(candidate) ||
      first.entity_count() != 1U) {
    return 8;
  }
  const auto other = first.begin_publication();
  const auto hidden = first.create_staged_entity(other);
  gneiss_scene_node_id parent{}, child{}, leaf{}, rejected{};
  const auto nested = first.begin_publication();
  auto& tree = first.scene();
  const gneiss::engine::core::math::transform identity = gneiss::engine::core::math::transform{};
  if (tree.create(0, replacement, &parent) != gneiss::engine::core::result::success ||
      tree.create_staged(other, parent, hidden, &child) != gneiss::engine::core::result::success ||
      tree.create_staged(other, child, 0, &leaf) != gneiss::engine::core::result::success ||
      tree.create_staged(nested, child, 0, &rejected) !=
          gneiss::engine::core::result::invalid_handle ||
      tree.create(child, 0, &rejected) != gneiss::engine::core::result::invalid_handle ||
      tree.set_local(child, identity) != gneiss::engine::core::result::invalid_handle ||
      tree.set_staged_local(nested, child, identity) !=
          gneiss::engine::core::result::invalid_handle ||
      tree.set_staged_local(other, child, identity) != gneiss::engine::core::result::success ||
      tree.destroy(child) != gneiss::engine::core::result::invalid_handle ||
      tree.destroy(parent) != gneiss::engine::core::result::not_ready ||
      tree.destroy_staged_leaf(other, child) != gneiss::engine::core::result::not_ready ||
      tree.destroy_staged_leaf(other, leaf) != gneiss::engine::core::result::success ||
      tree.destroy_staged_leaf(other, leaf) != gneiss::engine::core::result::invalid_handle ||
      tree.destroy_staged_leaf(other, child) != gneiss::engine::core::result::success ||
      tree.destroy(parent) != gneiss::engine::core::result::success) {
    return 11;
  }
  if (first.destroy_staged_entity(group, hidden) || !first.destroy_staged_entity(other, hidden) ||
      first.entity_count() != 1U) {
    return 9;
  }
  return 0;
}

int main() {
  try {
    return run_tests();
  } catch (...) {
    return 99;
  }
}
