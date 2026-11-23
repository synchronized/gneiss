// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <gneiss/engine/function/asset/asset_manager.hpp>
#include <gneiss/engine/function/scene/components.hpp>
#include <gneiss/engine/function/scene/world.hpp>
namespace gneiss::test_support {
inline gneiss::result<gneiss::game_object> mesh_target(gneiss::world& owner, scene_instance scene,
                                                       entity_id id) {
  if (scene.owner() != &owner)
    return gneiss::error::invalid_handle;
  auto destination = scene;
  auto object = owner.object(id);
  if (!object)
    return object.error();
  if (!destination.contains(*object))
    return gneiss::error::invalid_handle;
  return *object;
}
// 测试编排：显式查询缓存，再通过对象接口写入；不提供旧 expected/事务协议。
inline gneiss::error assign_mesh(gneiss::world& owner, gneiss::asset_manager& assets,
                                 scene_instance scene, entity_id id, std::string_view mesh_uri,
                                 std::string_view material_uri) {
  auto target = mesh_target(owner, scene, id);
  if (!target)
    return target.error();
  auto* current = target->try_component<gneiss::mesh_component>();
  auto before = current ? *current : gneiss::mesh_component{};
  if (gneiss::resource_uri(before.mesh) != mesh_uri) {
    auto loaded = assets.find<gneiss::mesh>(mesh_uri);
    if (!loaded)
      return loaded.error();
    before.mesh = *loaded;
  }
  if (gneiss::resource_uri(before.material) != material_uri) {
    auto loaded = assets.find<gneiss::material>(material_uri);
    if (!loaded)
      return loaded.error();
    before.material = *loaded;
  }
  auto object = owner.find(id);
  if (current) {
    return object.modify_component<gneiss::mesh_component>(
        [&](auto& component) noexcept { component = before; });
  }
  return object.add_component<gneiss::mesh_component>(
      gneiss::mesh_component{before.mesh, before.material});
}
struct mesh_fixture {
  std::string_view name;
  entity_id parent{};
  std::string_view mesh_uri, material_uri;
};
inline gneiss::result<entity_id> spawn_mesh(gneiss::world& owner, gneiss::asset_manager& assets,
                                            scene_instance scene, const mesh_fixture& fixture,
                                            const gneiss::mesh_component* retained = nullptr) {
  if (scene.owner() != &owner)
    return gneiss::error::invalid_handle;
  auto destination = scene;
  auto made = destination.create_object({fixture.name, {}}, owner.find(fixture.parent));
  if (!made)
    return made.error();
  // 运行对象夹具直接安装组件，不经描述反射转换。
  const auto status = retained ? made->add_component<gneiss::mesh_component>(*retained)
                               : assign_mesh(owner, assets, scene, made->entity(), fixture.mesh_uri,
                                             fixture.material_uri);
  if (status.failed()) {
    made->destroy();
    return status;
  }
  return made->entity();
}
}
