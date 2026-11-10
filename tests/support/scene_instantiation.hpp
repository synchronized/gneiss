// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/function/asset/asset_manager.hpp"
#include "engine/function/scene/scene_instantiation.hpp"
#include "scene_access.hpp"
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
                                                            scene_instantiation_data prepared,
                                                            const scene_resource_bindings& assets) {
  if (!owner.is_owner_thread())
    return error::invalid_state;
  {
    std::map<std::string, const prefab_description*, std::less<>> sources;
    for (const auto& [uri, source] : prepared.prefabs) {
      if (!source.resource)
        return error::invalid_argument;
      sources.emplace(uri, source.resource.get());
    }
    auto composed = compose_scene(prepared.description, sources);
    if (!composed)
      return composed.error();
    return instantiate_effective(owner, *gneiss::test_support::scene_access{owner},
                                 prepared.description.uuid, *composed, assets);
  }
}

inline result<scene_instantiation_result, scene_diagnostic>
instantiate_scene(world& owner, asset_manager& assets, ref<const scene> source,
                  std::size_t source_bytes = 0) {
  if (!owner.is_owner_thread())
    return scene_diagnostic{error::invalid_state, 0, {}, {}};
  auto prepared = make_instantiation_data(std::move(source), source_bytes);
  if (!prepared)
    return std::move(prepared).error();
  scene_resource_bindings bindings;
  for (const auto& request : prepared->assets) {
    auto loaded = assets.load(request);
    if (!loaded)
      return scene_diagnostic{loaded.error(), 0, request.uri, "无法提交场景视觉加载"};
    bindings.emplace(request.uri, loaded->resource());
  }
  auto requests = std::move(prepared->assets);
  auto result = instantiate_scene(owner, std::move(*prepared), bindings);
  if (!result)
    return scene_diagnostic{result.error(), 0, {}, {}};
  result->assets = std::move(requests);
  result->bindings = std::move(bindings);
  return std::move(*result);
}

}
