// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "../../support/async_application.hpp"
#include "../../support/scene_application.hpp"
#include "../../support/scene_instantiation.hpp"
#include "../../support/value_result.hpp"
#include <cstdio>
#include <gneiss/engine/function/meta_registration.hpp>
#include <gneiss/engine/function/scene/instantiation.hpp>
#include <gneiss/engine/function/scene/world.hpp>
#include <source_location>
#include <stdexcept>
namespace e = gneiss;
namespace {
void check(bool value, std::source_location where = std::source_location::current()) {
  if (!value)
    throw std::runtime_error("场景 Prefab 测试失败，行=" + std::to_string(where.line()));
}
}
int main() try {
  check(e::initialize_meta().ok());
  gneiss::runtime::application_configuration configuration;
  configuration.executor = &gneiss::test_support::application_tasks();
  configuration.asset_root = GNEISS_TEST_ASSET_ROOT;
  std::shared_ptr<gneiss::runtime::application> app;
  check(gneiss::test_support::assign_value(gneiss::runtime::application::create(configuration), app)
            .ok());
  auto world = gneiss::create_native();
  check(bool(world));
  e::scene_content prepared;
  e::scene_resource_bindings assets;
  check(gneiss::test_support::prepare_scene(*app, "asset://scenes/prefab.scene.json", prepared,
                                            assets)
            .ok());
  auto composition = gneiss::test_support::compose(prepared);
  check(bool(composition));
  auto created = gneiss::test_support::scene_fixture::instantiate_scene(**world, prepared, assets);
  check(created && created->node_count == 3 && created->objects.size() == 3);
  auto mapping = gneiss::test_support::associations(created->objects);
  const auto source_id = *e::guid::parse("30000000-0000-4000-8000-000000000002");
  auto member = std::ranges::find_if(
      composition->objects, [&](const auto& object) { return object.address.source == source_id; });
  check(member != composition->objects.end());
  check(member->effective.transform.translation.x == 3);
  const auto child = (*world)->find(mapping.at(member->address).entity);
  const auto root = (*world)->find(mapping.at(member->parent).entity);
  check(child.parent() == root);
  // 来源信息只在调用方组合结果中；运行对象只保存名称和普通组件。
  auto identity = root.name();
  check(root.is_valid());
  auto second = gneiss::test_support::scene_fixture::instantiate_scene(**world, prepared, assets);
  check(bool(second));
  auto second_mapping = gneiss::test_support::associations(second->objects);
  check(second_mapping.at(member->address).entity != child.entity());
  auto second_identity = (*world)->find(second_mapping.at(member->parent).entity).name();
  check(second_identity == identity && second_mapping.at(member->parent).entity != root.entity());
  child.set_name("Runtime edit");
  check(child.reparent({}).ok());
  check(!child.parent());
  check(member->effective.transform.translation.x == 3);
  check(child.reparent(root).ok());
  root.destroy();
  check(!child.is_valid());
  check((*world)->object(second_mapping.at(member->address).entity).has_value());
  created->instance.unload();
  second->instance.unload();
  check((*world)->entity_count() == 0);
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
