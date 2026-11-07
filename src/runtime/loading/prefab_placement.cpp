// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "runtime/loading/prefab_placement.hpp"
#include "engine/core/error.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/scene/prefab_replay.hpp"
#include "engine/function/scene/scene_types.hpp"
#include "engine/function/scene/structural_diff.hpp"
#include "engine/function/world/world.hpp"
#include "runtime/application.hpp"
#include <algorithm>

namespace gneiss::runtime {
gneiss::engine::error
prefab_placement::capture(runtime::application& application, gneiss::engine::guid uuid,
                          gneiss::scene_instance_id scene,
                          std::shared_ptr<engine::scene_subtree_snapshot>& snapshot) noexcept try {
  auto* host = &application;
  const auto validation = host->is_owner_thread() && host->accepts_requests()
                              ? gneiss::engine::error::success
                              : gneiss::engine::error::invalid_state;
  if (validation != gneiss::engine::error::success) {
    return validation;
  }
  auto root = application.scenes()->find_node(scene, uuid);
  if (!root)
    return root.error();
  auto captured = application.scenes()->capture_subtree(scene, *root);
  if (!captured)
    return captured.error();
  snapshot = std::make_shared<engine::scene_subtree_snapshot>(std::move(*captured));
  return engine::error::success;
} catch (const std::bad_alloc&) {
  return gneiss::engine::error::out_of_memory;
} catch (...) {
  return gneiss::engine::error::internal;
}

gneiss::engine::error prefab_placement::freeze(
    runtime::application& application, gneiss::engine::prefab_header_snapshot source,
    std::vector<gneiss::engine::render_resource_ref> assets,
    std::shared_ptr<const engine::scene_subtree_snapshot>& output) noexcept try {
  auto* host = &application;
  const auto validation = host->is_owner_thread() && host->accepts_requests()
                              ? gneiss::engine::error::success
                              : gneiss::engine::error::invalid_state;
  if (validation != gneiss::engine::error::success) {
    return validation;
  }
  auto value = std::make_shared<engine::scene_subtree_snapshot>();
  auto author = std::move(source.author);
  if (!source.source || !author.prefab)
    return engine::error::invalid_argument;
  // 来源冲突与实例化使用同一回放规则，资源取自成功应用后的有效描述。
  auto replayed = engine::replay_prefab_reference(*source.source, *author.prefab);
  if (!replayed)
    return replayed.error();
  const auto& effective = replayed->description;
  auto status = engine::error::success;
  value->root_uuid = author.uuid;
  value->parent_uuid = author.parent_uuid;
  author.parent_uuid = {};
  engine::scene_description description;
  description.uuid = author.uuid;
  description.objects.push_back(std::move(author));
  auto content = engine::capture_scene_record(description);
  if (!content)
    return content.error();
  value->content = std::move(*content);
  auto& binding = value->resources[value->root_uuid];
  auto frozen_source = engine::capture_prefab_record(*source.source);
  if (!frozen_source)
    return frozen_source.error();
  binding.prefab_content = std::move(*frozen_source);
  binding.source_asset = std::move(source.source_asset);
  binding.source_nodes.resize(source.source->objects.size());
  const auto find = [&](std::string_view uri, engine::render_resource_kind type,
                        engine::render_resource_ref& result) {
    const auto entry = std::ranges::find_if(assets, [&](const auto& asset) {
      return engine::resource_uri(asset) == uri && engine::resource_kind(asset) == type;
    });
    if (entry != assets.end()) {
      result = *entry;
      return engine::error::success;
    }
    // 刷新来源时，实例覆盖仍引用当前已加载资源；快照必须强持有它们。
    auto found =
        application.assets().find_loaded({std::string(uri), type}, application.assets().profile());
    if (!found)
      return found.error() == engine::error::not_found ? engine::error::not_ready : found.error();
    result = std::move(*found);
    return engine::error{engine::error::success};
  };
  for (std::size_t i = 0; i < binding.source_nodes.size(); ++i) {
    const auto* renderer = effective.objects[i].components.get<engine::mesh_renderer_description>();
    if (!renderer)
      continue;
    engine::render_resource_ref mesh, material;
    status = find(renderer->mesh_uri, engine::render_resource_kind::mesh, mesh);
    if (status.failed())
      return status;
    status = find(renderer->material_uri, engine::render_resource_kind::material, material);
    if (status.failed())
      return status;
    binding.source_nodes[i] = {engine::resource_cast<gneiss::engine::mesh>(mesh),
                               engine::resource_cast<gneiss::engine::material>(material)};
  }
  output = std::move(value);
  return gneiss::engine::error::success;
} catch (const std::bad_alloc&) {
  return gneiss::engine::error::out_of_memory;
} catch (...) {
  return gneiss::engine::error::internal;
}
gneiss::engine::error prefab_placement::apply(application& app, scene_instance_id scene,
                                              const engine::scene_subtree_snapshot& snapshot,
                                              bool removing, bool replacing,
                                              scene_node_id& output) noexcept try {
  using namespace gneiss::engine;
  output = {};
  if (!app.is_owner_thread() || !app.accepts_requests() || !app.scenes())
    return error::invalid_state;
  auto description = snapshot.description();
  if (!description)
    return description.error();
  if (removing) {
    if (replacing)
      return error::invalid_argument;
    auto root = app.scenes()->find_node(scene, snapshot.root_uuid);
    if (!root)
      return root.error();
    auto captured = app.scenes()->capture_subtree(scene, *root);
    if (!captured)
      return captured.error();
    auto current = captured->description();
    if (!current)
      return current.error();
    // 历史之外新增的对象不在快照中，不能随旧命令静默删除。
    if (current->objects.size() != description->objects.size())
      return error::invalid_state;
    for (const auto& object : current->objects)
      if (std::ranges::find(description->objects, object.uuid, &object_description::uuid) ==
          description->objects.end())
        return error::invalid_state;
    return app.scenes()->destroy_subtree(scene, *root);
  }
  if (replacing) {
    const auto root =
        std::ranges::find(description->objects, snapshot.root_uuid, &object_description::uuid);
    const auto resources = snapshot.resources.find(snapshot.root_uuid);
    if (root == description->objects.end() || resources == snapshot.resources.end())
      return error::invalid_argument;
    auto author = *root;
    if (!snapshot.parent_uuid.is_nil())
      author.parent_uuid = snapshot.parent_uuid;
    auto source = resources->second.prefab_source();
    if (!source)
      return source.error();
    auto scene_result = app.scenes()->instantiate_prefab_ready(scene, author, *source,
                                                               resources->second.source_nodes, true,
                                                               resources->second.source_asset);
    if (!scene_result)
      return scene_result.error();
    output = std::move(*scene_result);
    return gneiss::engine::error::success;
  }
  scene_node_id parent;
  if (!snapshot.parent_uuid.is_nil()) {
    auto status_value = app.scenes()->find_node(scene, snapshot.parent_uuid);
    if (!status_value)
      return status_value.error();
    parent = std::move(*status_value);
  }
  auto scene_result = app.scenes()->restore_subtree(scene, snapshot, parent, {});
  if (!scene_result)
    return scene_result.error();
  output = std::move(*scene_result);
  return gneiss::engine::error::success;
} catch (const std::bad_alloc&) {
  return gneiss::engine::error::out_of_memory;
} catch (...) {
  return gneiss::engine::error::internal;
}
}
