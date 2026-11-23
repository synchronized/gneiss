// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../../support/value_result.hpp"
#include "function/scene/world_state.hpp"
#include <gneiss/engine/core/base/error.hpp>
#include <gneiss/engine/core/base/rid.hpp>

#include <cstddef>

namespace {

struct position {
  int value;
};

} // namespace

int run_tests() {
  gneiss::world_state first{1};
  gneiss::world_state second{2};
  const auto entity = first.create_entity();
  first.emplace<position>(entity, 10);
  if (!first.is_alive(entity) || second.is_alive(entity) || first.entity_count() != 1U) {
    return 1;
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
  gneiss::entity_id parent{}, child{};
  if (gneiss::test_support::assign_value(first.create_object({}, replacement), parent).failed() ||
      gneiss::test_support::assign_value(first.create_object(parent, visible), child).failed() ||
      child.get() != visible || !first.destroy_entity(parent.get()) ||
      first.is_alive(replacement) || first.destroy_entity(visible) || first.entity_count() != 0U)
    return 7;
  // 辅助实体转为对象后只有一份场景归属，卸载仍覆盖全部成员。
  const gneiss::scene_instance_id scene{42};
  const auto auxiliary = first.create_entity();
  first.assign_scene(gneiss::entity_id{auxiliary}, scene);
  auto adopted = first.create_object({}, auxiliary);
  if (!adopted || first.scene_of(*adopted) != scene)
    return 12;
  std::size_t members = 0;
  first.each_scene_member(scene, [&](gneiss::entity_id) { ++members; });
  if (members != 1)
    return 13;
  const auto sibling = first.create_object({});
  const auto child_object = sibling ? first.create_object(*sibling) : sibling;
  const auto extra = first.create_entity();
  if (!sibling || !child_object)
    return 15;
  first.assign_scene(*sibling, scene);
  first.assign_scene(*child_object, scene);
  first.assign_scene(gneiss::entity_id{extra}, scene);
  first.destroy_scene_members(scene);
  if (first.is_alive(auxiliary) || first.entity_count() != 0)
    return 14;
  return 0;
}

int main() {
  try {
    return run_tests();
  } catch (...) {
    return 99;
  }
}
