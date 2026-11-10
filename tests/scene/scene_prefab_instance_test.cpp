// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "../support/async_application.hpp"
#include "../support/scene_access.hpp"
#include "../support/scene_application.hpp"
#include "../support/scene_instantiation.hpp"
#include "../support/value_result.hpp"
#include "engine/function/meta_registration.hpp"
#include "engine/function/scene/instantiation.hpp"
#include "engine/function/scene/world.hpp"
#include <cstdio>
#include <source_location>
#include <stdexcept>
namespace e = gneiss::engine;
namespace {
void check(bool value, std::source_location where = std::source_location::current()) {
  if (!value)
    throw std::runtime_error("场景 Prefab 测试失败，行=" + std::to_string(where.line()));
}
}
int main() try {
  check(e::initialize_meta().ok());
  gneiss::runtime::application_configuration configuration;
  configuration.asset_root = GNEISS_TEST_ASSET_ROOT;
  std::shared_ptr<gneiss::runtime::application> app;
  check(gneiss::test_support::assign_value(gneiss::runtime::application::create(configuration), app)
            .ok());
  e::scene_content prepared;
  e::scene_resource_bindings assets;
  check(gneiss::test_support::prepare_scene(*app, "asset://scenes/prefab.scene.json", prepared,
                                            assets)
            .ok());
  auto composition = gneiss::test_support::compose(prepared);
  check(bool(composition));
  auto created = gneiss::test_support::scene_fixture::instantiate_scene(*app->native_world(),
                                                                        prepared, assets);
  check(created && created->node_count == 3 && created->objects.size() == 3);
  auto mapping = gneiss::test_support::associations(created->objects);
  const auto source_id = *e::guid::parse("30000000-0000-4000-8000-000000000002");
  auto member = std::ranges::find_if(
      composition->objects, [&](const auto& object) { return object.address.source == source_id; });
  check(member != composition->objects.end());
  check(member->effective.transform.translation.x == 3);
  const auto child = mapping.at(member->address).node;
  const auto root = mapping.at(member->parent).node;
  gneiss::entity_id parent;
  check(gneiss::test_support::assign_value(
            app->native_world()->find(gneiss::entity_id{(child).get()}).parent(), parent)
            .ok() &&
        parent == root);
  // 来源信息只在调用方组合结果中；运行节点使用独立 UUID 和普通组件。
  auto identity = app->native_world()->find(gneiss::entity_id{(root).get()}).identity();
  check(identity && identity->uuid != member->address.object &&
        identity->uuid != member->parent.source);
  auto second = gneiss::test_support::scene_fixture::instantiate_scene(*app->native_world(),
                                                                       prepared, assets);
  check(bool(second));
  auto second_mapping = gneiss::test_support::associations(second->objects);
  check(second_mapping.at(member->address).node != child);
  auto second_identity =
      app->native_world()
          ->find(gneiss::entity_id{(second_mapping.at(member->parent).node).get()})
          .identity();
  check(second_identity && !second_identity->uuid && !identity->uuid);
  check(app->native_world()->object(gneiss::entity_id{child.get()})->set_name("Runtime edit").ok());
  check(gneiss::test_support::scene_access{*app}->reparent_node(created->instance, child, {}).ok());
  check(gneiss::test_support::assign_value(
            app->native_world()->find(gneiss::entity_id{(child).get()}).parent(), parent)
            .ok() &&
        !parent);
  check(member->effective.transform.translation.x == 3);
  check(
      gneiss::test_support::scene_access{*app}->reparent_node(created->instance, child, root).ok());
  check(gneiss::test_support::scene_access{*app}->destroy_subtree(created->instance, root).ok());
  check(!app->native_world()->object(gneiss::entity_id{child.get()}));
  check(app->native_world()->object(second_mapping.at(member->address).entity).has_value());
  check(gneiss::test_support::scene_access{*app}->unload(created->instance).ok());
  check(gneiss::test_support::scene_access{*app}->unload(second->instance).ok());
  check(*app->native_world()->entity_count() == 0);
  e::scene_content missing;
  check(gneiss::test_support::prepare_scene(*app, "asset://scenes/missing-prefab.scene.json",
                                            missing, assets)
            .failed());
  check(app->shutdown().ok());
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
