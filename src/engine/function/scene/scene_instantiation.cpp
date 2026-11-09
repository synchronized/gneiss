// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/function/scene/scene_instantiation.hpp"
#include "engine/function/asset/asset_manager.hpp"
#include "engine/function/scene/scene_application.hpp"
#include "engine/function/scene/scene_object.hpp"
namespace gneiss::engine {
namespace {
result<scene_instantiation_result> instantiate_effective(world& world, scene_instances& scenes,
                                                         guid uuid, const composed_scene& composed,
                                                         const scene_resource_bindings& assets) {
  entity_id previous_camera;
  const auto camera_status = world.get_active_camera(previous_camera);
  if (camera_status.failed() && camera_status != error::not_ready)
    return camera_status;
  auto created = scenes.create_empty(uuid);
  if (!created)
    return created.error();
  struct creation_guard {
    scene_instances& owner;
    scene_instance_id instance;
    engine::world& world;
    entity_id camera;
    ~creation_guard() {
      if (instance) {
        (void)owner.unload(instance);
        if (camera.get() != 0)
          (void)world.set_active_camera(camera);
      }
    }
  } guard{scenes, *created, world, previous_camera};
  std::map<object_address, scene_object_association> associations;
  auto applied = apply_scene(world, scenes, *created, associations, {}, composed, assets);
  if (!applied)
    return applied.error();
  if (!*applied)
    return error::invalid_state;
  scene_instantiation_result receipt;
  receipt.instance = *created;
  receipt.node_count = associations.size();
  receipt.objects.reserve(associations.size());
  for (const auto& object : composed.objects) {
    const auto& binding = associations.at(object.address);
    receipt.objects.push_back(
        {object.address.object, object.address.source, binding.node, binding.entity});
  }
  guard.instance = {};
  return receipt;
}
} // namespace
result<scene_instantiation_result> instantiate_scene(world& owner,
                                                     scene_instantiation_data prepared,
                                                     const scene_resource_bindings& assets) {
  if (!owner.is_owner_thread())
    return error::invalid_state;
  if (!owner.scenes() || !owner.scenes()->is_valid()) {
    return engine::error::invalid_argument;
  }
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
    return instantiate_effective(owner, *owner.scenes(), prepared.description.uuid, *composed,
                                 assets);
  }
}

namespace {
error validate(world& owner, scene_instance_id scene, scene_node_id node) {
  if (!owner.is_owner_thread())
    return error::invalid_state;
  return owner.scenes() ? owner.scenes()->validate_editable_node(scene, node)
                        : error{error::invalid_handle};
}
}
result<scene_instantiation_result, scene_diagnostic> instantiate_scene(world& owner,
                                                                       asset_manager& assets,
                                                                       ref<const scene> source,
                                                                       std::size_t source_bytes) {
  if (!owner.is_owner_thread())
    return scene_diagnostic{error::invalid_state, 0, {}, {}};
  if (!owner.scenes() || !owner.scenes()->is_valid())
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
result<scene_node_id> create_mesh_renderer_node(world& owner, asset_manager& assets,
                                                scene_instance_id instance,
                                                const mesh_renderer_node_creation& desc,
                                                const scene_mesh_binding* prepared) {
  if (!owner.is_owner_thread())
    return error::invalid_state;
  if (!owner.scenes())
    return error::invalid_handle;
  scene_mesh_binding binding;
  if (prepared) {
    if (!assets.owns(prepared->mesh) || !assets.owns(prepared->material) ||
        resource_uri(prepared->mesh) != desc.mesh_uri ||
        resource_uri(prepared->material) != desc.material_uri)
      return error::invalid_argument;
    binding = *prepared;
  } else {
    auto mesh = assets.find<engine::mesh>(desc.mesh_uri);
    if (!mesh)
      return mesh.error();
    auto material = assets.find<engine::material>(desc.material_uri);
    if (!material)
      return material.error();
    binding = {*mesh, *material};
  }
  auto created = owner.scenes()->create_node(instance, {desc.uuid, desc.name, desc.parent, {}});
  if (!created)
    return created.error();
  scene_subtree_guard guard(owner, *created);
  auto description = make_component_description(
      mesh_renderer_description{std::string(desc.mesh_uri), std::string(desc.material_uri)});
  auto status = add_component({owner, *created, {binding.mesh, binding.material}, &assets},
                              description, desc.component_uuid);
  if (status.failed())
    return status;
  guard.release();
  return *created;
}
error set_mesh_renderer(world& owner, asset_manager& assets, scene_instance_id scene,
                        scene_node_id node, std::string_view mesh_uri,
                        std::string_view material_uri) {
  auto status = validate(owner, scene, node);
  if (status.failed())
    return status;
  auto current = owner.node_mesh_renderer(node);
  if (!current && current.error() != error::not_found)
    return current.error();
  auto binding = current ? *current : mesh_renderer{};
  if (resource_uri(binding.mesh) != mesh_uri) {
    if (mesh_uri.empty())
      binding.mesh = {};
    else {
      auto loaded = assets.find<engine::mesh>(mesh_uri);
      if (!loaded)
        return loaded.error();
      binding.mesh = *loaded;
    }
  }
  if (resource_uri(binding.material) != material_uri) {
    if (material_uri.empty())
      binding.material = {};
    else {
      auto loaded = assets.find<engine::material>(material_uri);
      if (!loaded)
        return loaded.error();
      binding.material = *loaded;
    }
  }
  return owner.entity_set_mesh_renderer(entity_id{node.get()}, binding);
}
result<scene_mesh_binding> capture_mesh_binding(world& owner, scene_instance_id scene,
                                                scene_node_id node) {
  auto status = validate(owner, scene, node);
  if (status.failed())
    return status;
  auto current = owner.node_mesh_renderer(node);
  if (!current)
    return current.error() == error::not_found ? result<scene_mesh_binding>{scene_mesh_binding{}}
                                               : result<scene_mesh_binding>{current.error()};
  return scene_mesh_binding{current->mesh, current->material};
}
error replace_mesh_binding(world& owner, scene_instance_id scene, scene_node_id node,
                           const scene_mesh_binding& expected,
                           const scene_mesh_binding& replacement, guid component_uuid,
                           asset_manager* resources) {
  auto current = capture_mesh_binding(owner, scene, node);
  if (!current)
    return current.error();
  if (resources && ((replacement.mesh && !resources->owns(replacement.mesh)) ||
                    (replacement.material && !resources->owns(replacement.material))))
    return error::invalid_argument;
  if (current->mesh != expected.mesh || current->material != expected.material)
    return error::invalid_state;
  auto existing = owner.node_mesh_renderer(node);
  if (!existing && existing.error() != error::not_found)
    return existing.error();
  if (!existing)
    return add_component({owner, node, {replacement.mesh, replacement.material}, resources},
                         make_component_description(mesh_renderer_description{
                             std::string(resource_uri(replacement.mesh)),
                             std::string(resource_uri(replacement.material))}),
                         component_uuid);
  const mesh_renderer_replacement change{entity_id{node.get()},
                                         {expected.mesh, expected.material},
                                         {replacement.mesh, replacement.material}};
  return owner.replace_mesh_renderers({&change, 1});
}
}
