// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

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

gneiss_result update_position(gneiss::world_internal::world_state& world,
                              void* user_data) noexcept {
  auto& context = *static_cast<system_context*>(user_data);
  context.events[context.count++] = 1;
  try {
    world.each<position>([&context](position& value) { value.value += context.delta; });
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result fail_system(gneiss::world_internal::world_state& /*world*/,
                          void* user_data) noexcept {
  auto& context = *static_cast<system_context*>(user_data);
  context.events[context.count++] = 2;
  return GNEISS_ERROR_INVALID_STATE;
}

gneiss_result skipped_system(gneiss::world_internal::world_state& /*world*/,
                             void* user_data) noexcept {
  auto& context = *static_cast<system_context*>(user_data);
  context.events[context.count++] = 3;
  return GNEISS_SUCCESS;
}

} // namespace

int run_tests() {
  gneiss::world_internal::world_state first{1};
  gneiss::world_internal::world_state second{2};
  const auto entity = first.create_entity();
  first.emplace<position>(entity, 10);
  if (!first.is_alive(entity) || second.is_alive(entity) || first.entity_count() != 1U) {
    return 1;
  }

  system_context context{.delta = 5};
  gneiss::world_internal::system_scheduler scheduler;
  if (scheduler.add(1, update_position, &context) != GNEISS_SUCCESS ||
      scheduler.add(2, fail_system, &context) != GNEISS_SUCCESS ||
      scheduler.add(3, skipped_system, &context) != GNEISS_SUCCESS ||
      scheduler.add(3, skipped_system, &context) != GNEISS_ERROR_INVALID_ARGUMENT ||
      scheduler.run(first) != GNEISS_ERROR_INVALID_STATE) {
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
  gneiss_result other_thread_result = GNEISS_SUCCESS;
  std::thread worker([&] { other_thread_result = first.publish(group); });
  worker.join();
  if (other_thread_result != GNEISS_ERROR_INVALID_STATE) {
    return 10;
  }
  if (candidate == GNEISS_NULL_ENTITY_ID || !first.emplace_staged<position>(group, candidate, 40) ||
      first.is_alive(candidate) || first.get<position>(candidate) != nullptr ||
      first.entity_count() != 1U || first.destroy_entity(candidate) ||
      second.create_staged_entity(group) != GNEISS_NULL_ENTITY_ID ||
      second.publish(group) != GNEISS_ERROR_INVALID_STATE) {
    return 6;
  }
  int visited = 0;
  first.each<position>([&](const position&) { ++visited; });
  if (visited != 0 || !first.destroy_staged_entity(group, cancelled) ||
      first.destroy_staged_entity(group, cancelled) || first.publish(group) != GNEISS_SUCCESS ||
      first.entity_count() != 2U || !first.is_alive(candidate) ||
      first.get<position>(candidate)->value != 40 ||
      first.publish(group) != GNEISS_ERROR_INVALID_STATE ||
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
  const gneiss_transform identity = GNEISS_TRANSFORM_IDENTITY;
  if (tree.create(0, replacement, &parent) != GNEISS_SUCCESS ||
      tree.create_staged(other, parent, hidden, &child) != GNEISS_SUCCESS ||
      tree.create_staged(other, child, 0, &leaf) != GNEISS_SUCCESS ||
      tree.create_staged(nested, child, 0, &rejected) != GNEISS_ERROR_INVALID_HANDLE ||
      tree.create(child, 0, &rejected) != GNEISS_ERROR_INVALID_HANDLE ||
      tree.set_local(child, identity) != GNEISS_ERROR_INVALID_HANDLE ||
      tree.set_staged_local(nested, child, identity) != GNEISS_ERROR_INVALID_HANDLE ||
      tree.set_staged_local(other, child, identity) != GNEISS_SUCCESS ||
      tree.destroy(child) != GNEISS_ERROR_INVALID_HANDLE ||
      tree.destroy(parent) != GNEISS_ERROR_NOT_READY ||
      tree.destroy_staged_leaf(other, child) != GNEISS_ERROR_NOT_READY ||
      tree.destroy_staged_leaf(other, leaf) != GNEISS_SUCCESS ||
      tree.destroy_staged_leaf(other, leaf) != GNEISS_ERROR_INVALID_HANDLE ||
      tree.destroy_staged_leaf(other, child) != GNEISS_SUCCESS ||
      tree.destroy(parent) != GNEISS_SUCCESS) {
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
