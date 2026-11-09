// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "../support/scene_application.hpp"
#include "engine/function/meta_registration.hpp"
#include "engine/function/world/world.hpp"
#include <cstdio>
#include <source_location>
#include <stdexcept>
#include <thread>
namespace e = gneiss::engine;
namespace {
void check(bool value, std::source_location where = std::source_location::current()) {
  if (!value)
    throw std::runtime_error("Prefab 通用应用测试失败，行=" + std::to_string(where.line()));
}
}
int main() try {
  check(e::initialize_meta().ok());
  auto owner = e::create_native();
  check(bool(owner));
  auto& world = **owner;
  auto attached = e::scene_instances::attach(world);
  check(bool(attached));
  auto& scenes = **attached;
  auto instance = scenes.create_empty(e::guid::generate());
  check(bool(instance));
  auto source = std::make_shared<e::prefab_description>();
  source->uuid = e::guid::generate();
  e::game_object_description root, child;
  root.uuid = e::guid::generate();
  root.name = "Root";
  child.uuid = e::guid::generate();
  child.parent.object = root.uuid;
  child.name = "Child";
  child.transform.translation = {0, 2, 0};
  source->root_uuid = root.uuid;
  source->objects = {child, root};
  e::scene_description author;
  author.uuid = e::guid::generate();
  e::game_object_description first, second;
  first.uuid = e::guid::generate();
  first.prefab = e::prefab_reference_description{"asset://source", {}, {}};
  first.name = "First";
  second = first;
  second.uuid = e::guid::generate();
  second.name = "Second";
  author.objects = {first, second};
  auto effective = e::compose_scene(author, {{"asset://source", source.get()}});
  check(effective && effective->objects.size() == 4);
  std::map<e::object_address, e::scene_object_association> mapping;
  auto applied = e::apply_scene(world, scenes, *instance, mapping, {}, *effective, {});
  check(applied && *applied);
  const e::object_address first_root{first.uuid, root.uuid}, first_child{first.uuid, child.uuid};
  const e::object_address second_root{second.uuid, root.uuid};
  const auto root_node = mapping.at(first_root).node;
  const auto child_node = mapping.at(first_child).node;
  check(root_node != mapping.at(second_root).node);
  auto root_identity = world.node_identity(root_node);
  auto other_identity = world.node_identity(mapping.at(second_root).node);
  check(root_identity && other_identity && root_identity->uuid != other_identity->uuid);
  check(root_identity->uuid != first.uuid && root_identity->uuid != root.uuid);
  gneiss::scene_node_id parent;
  check(world.node_get_parent(child_node, parent).ok() && parent == root_node);
  // 来源描述释放不影响普通运行成员；组合结果由外部调用方持有。
  std::weak_ptr<e::prefab_description> lifetime = source;
  source.reset();
  check(lifetime.expired() && world.object(mapping.at(first_child).entity).has_value());
  auto next = *effective;
  for (auto& object : next.objects) {
    if (object.address == first_child) {
      object.effective.name = "Renamed";
      object.effective.transform.translation = {3, 4, 5};
      object.parent = second_root;
    }
  }
  applied = e::apply_scene(world, scenes, *instance, mapping, *effective, next, {});
  check(applied && *applied && mapping.at(first_child).node == child_node);
  check(world.node_get_parent(child_node, parent).ok() && parent == mapping.at(second_root).node);
  check(world.node_identity(child_node)->name == "Renamed");
  // 无效目标层级在修改前拒绝。
  auto cycle = next;
  for (auto& object : cycle.objects)
    if (object.address == second_root)
      object.parent = first_child;
  applied = e::apply_scene(world, scenes, *instance, mapping, next, cycle, {});
  check(!applied && applied.error() == e::error::invalid_argument);
  check(world.node_get_parent(child_node, parent).ok() && parent == mapping.at(second_root).node);
  e::error foreign;
  std::thread worker([&] {
    auto result = e::apply_scene(world, scenes, *instance, mapping, next, next, {});
    foreign = result ? e::error::success : result.error();
  });
  worker.join();
  check(foreign == e::error::invalid_state);
  // 移除单个地址只销毁该成员，另一个实例的同源对象保留。
  auto removed = next;
  std::erase_if(removed.objects, [&](const auto& object) { return object.address == first_child; });
  applied = e::apply_scene(world, scenes, *instance, mapping, next, removed, {});
  check(applied && *applied && !mapping.contains(first_child));
  check(!world.object(gneiss::entity_id{child_node.get()}));
  check(mapping.at(first_root).node == root_node && *scenes.get_node_count(*instance) == 3);
  // 外部直接销毁导致失步，不得误用旧实体或静默部分更新。
  check(world.object_destroy(root_node).ok());
  applied = e::apply_scene(world, scenes, *instance, mapping, removed, removed, {});
  check(applied && !*applied);
  check(scenes.unload(*instance).ok() && *world.entity_count() == 0);
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
