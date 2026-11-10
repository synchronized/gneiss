// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/function/asset/asset_manager.hpp"
#include "engine/function/scene/scene_instantiation.hpp"
#include "scene_access.hpp"
#include "scene_application.hpp"
// 测试夹具保留便捷创建，不作为引擎实例化接口。
namespace gneiss::test_support::scene_fixture {
using namespace engine;
namespace {
inline result<scene_instantiation_result>
instantiate_effective(world& world, scene_access scenes, guid uuid, const composed_scene& composed,
                      const scene_resource_bindings& assets) {
  auto created = scenes.create_empty(uuid);
  if (!created)
    return created.error();
  struct creation_guard {
    scene_access owner;
    scene_instance_id instance;
    ~creation_guard() {
      if (instance) {
        (void)owner.unload(instance);
      }
    }
  } guard{scenes, *created};
  auto destination = scene_instance::borrow(world, *created);
  if (!destination)
    return destination.error();
  auto receipt = instantiate(*destination, composed, assets);
  if (!receipt)
    return receipt.error();
  guard.instance = {};
  return receipt;
}
} // namespace
inline result<scene_instantiation_result> instantiate_scene(world& owner,
                                                            const scene_content& prepared,
                                                            const scene_resource_bindings& assets) {
  if (!owner.is_owner_thread())
    return error::invalid_state;
  auto composed = gneiss::test_support::compose(prepared);
  if (!composed)
    return composed.error();
  return instantiate_effective(owner, *gneiss::test_support::scene_access{owner},
                               prepared.description->uuid, *composed, assets);
}

inline result<scene_instantiation_result, scene_diagnostic>
instantiate_scene(world& owner, asset_manager& assets, ref<const scene> source) {
  if (!owner.is_owner_thread())
    return scene_diagnostic{error::invalid_state, 0, {}, {}};
  if (!source || !source->content())
    return scene_diagnostic{error::invalid_argument, 0, {}, {}};
  auto prepared = source->content();
  auto composed = gneiss::test_support::compose(*prepared);
  if (!composed)
    return scene_diagnostic{composed.error(), 0, {}, {}};
  auto dependencies = scene_dependencies(*composed);
  if (!dependencies)
    return scene_diagnostic{dependencies.error(), 0, {}, {}};
  scene_resource_bindings bindings;
  for (const auto& request : *dependencies) {
    auto loaded = assets.load(request);
    if (!loaded)
      return scene_diagnostic{loaded.error(), 0, request.uri, "无法提交场景视觉加载"};
    bindings.emplace(request.uri, loaded->resource());
  }
  auto requests = std::move(*dependencies);
  auto result = instantiate_scene(owner, *prepared, bindings);
  if (!result)
    return scene_diagnostic{result.error(), 0, {}, {}};
  result->assets = std::move(requests);
  result->bindings = std::move(bindings);
  return std::move(*result);
}

}
