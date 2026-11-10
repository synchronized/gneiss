// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/function/asset/asset_manager.hpp"
#include "engine/function/scene/component_registration.hpp"
#include "engine/function/scene/components.hpp"
#include "engine/function/scene/scene_mesh_binding.hpp"
namespace gneiss::test_support {
inline engine::result<engine::game_object> mesh_target(engine::world& owner,
                                                       scene_instance_id scene, entity_id id) {
  auto destination = engine::scene_instance::borrow(owner, scene);
  if (!destination)
    return destination.error();
  auto object = owner.object(id);
  if (!object)
    return object.error();
  auto member = destination->contains(*object);
  if (!member)
    return member.error();
  if (!*member)
    return engine::error::invalid_handle;
  return *object;
}
inline engine::result<engine::scene_mesh_binding> read_mesh(engine::world& owner,
                                                            scene_instance_id scene, entity_id id) {
  auto object = mesh_target(owner, scene, id);
  if (!object)
    return object.error();
  auto component = object->read_component<engine::mesh_renderer_component>();
  if (!component)
    return component.error() == engine::error::not_found
               ? engine::result<engine::scene_mesh_binding>{engine::scene_mesh_binding{}}
               : engine::result<engine::scene_mesh_binding>{component.error()};
  return engine::scene_mesh_binding{component->value.mesh, component->value.material};
}
// 测试编排：显式查询缓存，再通过对象接口写入；不提供旧 expected/事务协议。
inline engine::error assign_mesh(engine::world& owner, engine::asset_manager& assets,
                                 scene_instance_id scene, entity_id id, std::string_view mesh_uri,
                                 std::string_view material_uri) {
  auto before = read_mesh(owner, scene, id);
  if (!before)
    return before.error();
  if (engine::resource_uri(before->mesh) != mesh_uri) {
    auto loaded = assets.find<engine::mesh>(mesh_uri);
    if (!loaded)
      return loaded.error();
    before->mesh = *loaded;
  }
  if (engine::resource_uri(before->material) != material_uri) {
    auto loaded = assets.find<engine::material>(material_uri);
    if (!loaded)
      return loaded.error();
    before->material = *loaded;
  }
  return owner.find(id).set_component(
      engine::mesh_renderer_component{{before->mesh, before->material}});
}
struct mesh_fixture {
  engine::guid uuid;
  std::string_view name;
  entity_id parent{};
  std::string_view mesh_uri, material_uri;
};
inline engine::result<entity_id> spawn_mesh(engine::world& owner, engine::asset_manager& assets,
                                            scene_instance_id scene, const mesh_fixture& fixture,
                                            const engine::scene_mesh_binding* retained = nullptr) {
  auto destination = engine::scene_instance::borrow(owner, scene);
  if (!destination)
    return destination.error();
  auto made = destination->create({fixture.uuid, std::string(fixture.name)}, {},
                                  owner.find(fixture.parent));
  if (!made)
    return made.error();
  engine::component_context context{*made};
  context.lookup_resource = [&](const engine::render_resource_source& source)
      -> engine::result<engine::render_resource_ref> {
    if (retained) {
      if (source.type == engine::render_resource_kind::mesh)
        return engine::render_resource_ref{retained->mesh};
      return engine::render_resource_ref{retained->material};
    }
    if (source.type == engine::render_resource_kind::mesh) {
      auto value = assets.find<engine::mesh>(source.uri);
      if (!value)
        return value.error();
      return engine::render_resource_ref{*value};
    }
    auto value = assets.find<engine::material>(source.uri);
    if (!value)
      return value.error();
    return engine::render_resource_ref{*value};
  };
  auto status = engine::add_component(
      context, engine::make_component_description(engine::mesh_renderer_description{
                   std::string(fixture.mesh_uri), std::string(fixture.material_uri)}));
  if (status.failed()) {
    (void)made->destroy();
    return status;
  }
  return made->entity();
}
}
