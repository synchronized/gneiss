// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/value_result.hpp"
#include "engine/core/error.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/scene/detail/system_scheduler.hpp"
#include "engine/function/scene/detail/world_state.hpp"

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
      tree.get_local(child) != nullptr || first.is_alive(replacement) ||
      first.destroy_entity(visible) || first.entity_count() != 0U)
    return 7;
  // 节点和实体共用身份；跨 World、generation 和重复删除同时失效。
  const auto root = tree.create({}, 0);
  const auto leaf =
      root ? tree.create(*root, 0)
           : gneiss::engine::result<gneiss::scene_node_id>{gneiss::engine::error::internal};
  if (!root || !leaf || root->get() != tree.get_entity(*root) || second.scene().get_local(*root) ||
      second.scene().create(*root, 0).error() != gneiss::engine::error::invalid_handle ||
      tree.reparent(*leaf, *root).failed() || tree.children(*root)->size() != 1 ||
      tree.reparent(*root, *leaf) != gneiss::engine::error::invalid_argument)
    return 8;
  // 先建子节点，再挂父节点，删除时会触发 EnTT 紧凑组件存储移动。
  const auto earlier = tree.create({}, 0);
  const auto later = tree.create({}, 0);
  if (!earlier || !later || tree.reparent(*earlier, *later).failed() ||
      !first.destroy_entity(later->get()) || first.is_alive(earlier->get()) ||
      tree.get_local(*later) || tree.get_local(*earlier))
    return 9;
  if (!first.destroy_entity(root->get()) || first.is_alive(leaf->get()) ||
      tree.destroy(*root) != gneiss::engine::error::invalid_handle || first.entity_count() != 0)
    return 10;
  const auto reused = tree.create({}, 0);
  if (!reused || *reused == *root || *reused == *leaf || tree.get_local(*root) ||
      tree.get_local(*leaf))
    return 11;
  return 0;
}

int main() {
  try {
    return run_tests();
  } catch (...) {
    return 99;
  }
}
