// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "../support/async_application.hpp"
#include "../support/scene_application.hpp"
#include "../support/value_result.hpp"
#include "engine/function/meta_registration.hpp"
#include "engine/function/world/world.hpp"
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
  e::scene_instantiation_data prepared;
  e::scene_instances::resource_bindings assets;
  check(gneiss::test_support::prepare_scene(*app, "asset://scenes/prefab.scene.json", prepared,
                                            assets)
            .ok());
  auto composition = gneiss::test_support::compose(prepared);
  check(bool(composition));
  auto created = app->scenes()->instantiate_prepared(prepared, assets);
  check(created && created->node_count == 3 && created->objects.size() == 3);
  auto mapping = gneiss::test_support::associations(created->objects);
  const auto source_id = *e::guid::parse("30000000-0000-4000-8000-000000000002");
  auto member = std::ranges::find_if(
      composition->objects, [&](const auto& object) { return object.address.source == source_id; });
  check(member != composition->objects.end() && !member->instance_root);
  check(member->effective.transform.translation.x == 3 && !member->overrides.empty());
  const auto child = mapping.at(member->address).node;
  const auto root = mapping.at(member->parent).node;
  gneiss::scene_node_id parent;
  check(app->native_world()->node_get_parent(child, parent).ok() && parent == root);
  // 来源信息只在调用方组合结果中；运行节点使用独立 UUID 和普通组件。
  auto identity = app->native_world()->node_identity(root);
  check(identity && identity->uuid != member->address.object &&
        identity->uuid != member->parent.source);
  auto second = app->scenes()->instantiate_prepared(prepared, assets);
  check(bool(second));
  auto second_mapping = gneiss::test_support::associations(second->objects);
  check(second_mapping.at(member->address).node != child);
  auto second_identity = app->native_world()->node_identity(second_mapping.at(member->parent).node);
  check(second_identity && second_identity->uuid != identity->uuid);
  check(app->native_world()->object(gneiss::entity_id{child.get()})->set_name("Runtime edit").ok());
  check(app->scenes()->reparent_node(created->instance, child, {}).ok());
  check(app->native_world()->node_get_parent(child, parent).ok() && !parent);
  check(member->effective.transform.translation.x == 3);
  check(app->scenes()->reparent_node(created->instance, child, root).ok());
  check(app->scenes()->destroy_subtree(created->instance, root).ok());
  check(!app->native_world()->object(gneiss::entity_id{child.get()}));
  check(app->native_world()->object(second_mapping.at(member->address).entity).has_value());
  check(app->scenes()->unload(created->instance).ok());
  check(app->scenes()->unload(second->instance).ok());
  check(*app->native_world()->entity_count() == 0);
  e::scene_instantiation_data missing;
  check(gneiss::test_support::prepare_scene(*app, "asset://scenes/missing-prefab.scene.json",
                                            missing, assets)
            .failed());
  check(app->shutdown().ok());
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
