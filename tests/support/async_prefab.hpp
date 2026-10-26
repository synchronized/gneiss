// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "async_application.hpp"
namespace gneiss::test_support {
// 测试驱动异步准备；最终实例化只使用已准备描述与引用。
inline engine::result instantiate_prefab(runtime::application& app, scene_instance_id scene,
                                         const engine::prefab_instance_description& author,
                                         scene_node_id& root, bool replacing = false,
                                         engine::task_scheduler* tasks = nullptr) {
  root = {};
  engine::prepared_scene_description prepared;
  engine::scene_instances::asset_bindings assets;
  auto status = prepare_scene(app, author.prefab_uri, prepared, assets, tasks, true);
  if (status.failed())
    return status;
  auto source = prepared.prefabs.at(author.prefab_uri).resource;
  std::vector<engine::scene_mesh_binding> bindings(source->objects.size());
  for (std::size_t i = 0; i < bindings.size(); ++i) {
    const auto& object = source->objects[i];
    if (!object.mesh_renderer)
      continue;
    bindings[i] = {
        engine::asset_cast<engine::mesh_resource>(assets.at(object.mesh_renderer->mesh_uri)),
        engine::asset_cast<engine::material_resource>(
            assets.at(object.mesh_renderer->material_uri))};
  }
  return app.scenes()->instantiate_prefab_ready(scene, author, source, bindings, replacing, root);
}
inline engine::result create_prefab(runtime::application& app, scene_instance_id scene,
                                    const engine::prefab_creation& creation, scene_node_id& root,
                                    engine::task_scheduler* tasks = nullptr) {
  root = {};
  engine::prefab_instance_description author;
  author.instance_uuid = creation.instance_uuid;
  author.name = creation.name;
  author.prefab_uri = creation.prefab_uri;
  author.translation = creation.local_transform.translation;
  author.rotation = creation.local_transform.rotation;
  author.scale = creation.local_transform.scale;
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
      return engine::result::invalid_handle;
  }
  return instantiate_prefab(app, scene, author, root, false, tasks);
}
inline engine::result refresh_prefab(runtime::application& app, scene_instance_id scene,
                                     std::string_view uuid, scene_node_id& root) {
  engine::prefab_instance_description author;
  engine::prefab_asset_ref source;
  auto status = app.scenes()->capture_prefab_header(scene, uuid, author, source);
  if (status.failed())
    return status;
  return instantiate_prefab(app, scene, author, root, true);
}
}
