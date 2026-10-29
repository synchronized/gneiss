// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "async_application.hpp"
#include "engine/core/guid.hpp"
namespace gneiss::test_support {
// 测试驱动异步准备；最终实例化只使用已准备描述与引用。
inline engine::error instantiate_prefab(runtime::application& app, scene_instance_id scene,
                                        const engine::object_description& author,
                                        scene_node_id& root, bool replacing = false,
                                        engine::task_scheduler* tasks = nullptr) {
  root = {};
  engine::prepared_scene_description prepared;
  engine::scene_instances::asset_bindings assets;
  auto status = prepare_scene(app, author.prefab->uri, prepared, assets, tasks, true);
  if (status.failed())
    return status;
  auto source = prepared.prefabs.at(author.prefab->uri).resource;
  std::vector<engine::scene_mesh_binding> bindings(source->objects.size());
  for (std::size_t i = 0; i < bindings.size(); ++i) {
    const auto& object = source->objects[i];
    if (!object.components.get<engine::mesh_renderer_description>())
      continue;
    bindings[i] = {engine::asset_cast<engine::mesh_resource>(assets.at(
                       object.components.get<engine::mesh_renderer_description>()->mesh_uri)),
                   engine::asset_cast<engine::material_resource>(assets.at(
                       object.components.get<engine::mesh_renderer_description>()->material_uri))};
  }
  return app.scenes()->instantiate_prefab_ready(scene, author, source, bindings, replacing, root);
}
inline engine::error create_prefab(runtime::application& app, scene_instance_id scene,
                                   const engine::prefab_creation& creation, scene_node_id& root,
                                   engine::task_scheduler* tasks = nullptr) {
  root = {};
  engine::object_description author;
  author.prefab.emplace();
  author.uuid = creation.instance_uuid;
  author.name = creation.name;
  author.prefab->uri = creation.prefab_uri;
  author.transform = creation.local_transform;
  if (creation.parent) {
    std::uint64_t count{};
    auto status = app.scenes()->get_node_count(scene, &count);
    if (status.failed())
      return status;
    for (std::uint64_t i = 0; i < count; ++i) {
      engine::scene_node_view node;
      status = app.scenes()->get_node_info(scene, i, node);
      if (status.failed())
        return status;
      if (node.node == creation.parent) {
        author.parent_uuid = node.uuid;
        break;
      }
    }
    if (!author.parent_uuid)
      return engine::error::invalid_handle;
  }
  return instantiate_prefab(app, scene, author, root, false, tasks);
}
inline engine::error refresh_prefab(runtime::application& app, scene_instance_id scene,
                                    gneiss::engine::guid uuid, scene_node_id& root) {
  engine::object_description author;
  engine::prefab_asset_ref source;
  auto status = app.scenes()->capture_prefab_header(scene, uuid, author, source);
  if (status.failed())
    return status;
  return instantiate_prefab(app, scene, author, root, true);
}
}
