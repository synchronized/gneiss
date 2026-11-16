// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <gneiss/engine/function/asset/asset_manager.hpp>
#include <gneiss/engine/function/scene/component_registration.hpp>
#include <gneiss/engine/function/scene/components.hpp>
namespace gneiss::test_support {
inline gneiss::result<gneiss::game_object> mesh_target(gneiss::world& owner, scene_instance scene,
                                                       entity_id id) {
  if (scene.owner() != &owner)
    return gneiss::error::invalid_handle;
  auto destination = scene;
  auto object = owner.object(id);
  if (!object)
    return object.error();
  auto member = destination.contains(*object);
  if (!member)
    return member.error();
  if (!*member)
    return gneiss::error::invalid_handle;
  return *object;
}
inline gneiss::result<gneiss::mesh_component> read_mesh(gneiss::world& owner,
                                                            scene_instance scene, entity_id id) {
  auto object = mesh_target(owner, scene, id);
  if (!object)
    return object.error();
  auto component = object->try_component<gneiss::mesh_component>();
  if (!component)
    return gneiss::mesh_component{};
  return gneiss::mesh_component{component->mesh, component->material};
}
// 测试编排：显式查询缓存，再通过对象接口写入；不提供旧 expected/事务协议。
inline gneiss::error assign_mesh(gneiss::world& owner, gneiss::asset_manager& assets,
                                 scene_instance scene, entity_id id, std::string_view mesh_uri,
                                 std::string_view material_uri) {
  auto before = read_mesh(owner, scene, id);
  if (!before)
    return before.error();
  if (gneiss::resource_uri(before->mesh) != mesh_uri) {
    auto loaded = assets.find<gneiss::mesh>(mesh_uri);
    if (!loaded)
      return loaded.error();
    before->mesh = *loaded;
  }
  if (gneiss::resource_uri(before->material) != material_uri) {
    auto loaded = assets.find<gneiss::material>(material_uri);
    if (!loaded)
      return loaded.error();
    before->material = *loaded;
  }
  auto object = owner.find(id);
  if (auto* component = object.try_component<gneiss::mesh_component>()) {
    *component = {before->mesh, before->material};
    return object.notify_component_modified<gneiss::mesh_component>();
  }
  return object.add_component<gneiss::mesh_component>(
      gneiss::mesh_component{before->mesh, before->material});
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
  gneiss::component_context context{*made};
  context.lookup_resource = [&](const gneiss::render_resource_source& source)
      -> gneiss::result<gneiss::render_resource_ref> {
    if (retained) {
      if (source.type == gneiss::render_resource_kind::mesh)
        return gneiss::render_resource_ref{retained->mesh};
      return gneiss::render_resource_ref{retained->material};
    }
    if (source.type == gneiss::render_resource_kind::mesh) {
      auto value = assets.find<gneiss::mesh>(source.uri);
      if (!value)
        return value.error();
      return gneiss::render_resource_ref{*value};
    }
    auto value = assets.find<gneiss::material>(source.uri);
    if (!value)
      return value.error();
    return gneiss::render_resource_ref{*value};
  };
  auto status = gneiss::add_component(
      context, gneiss::make_component_description(gneiss::mesh_component_description{
                   std::string(fixture.mesh_uri), std::string(fixture.material_uri)}));
  if (status.failed()) {
    (void)made->destroy();
    return status;
  }
  return made->entity();
}
}
