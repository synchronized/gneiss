// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "runtime/loading/prefab_placement.hpp"
#include "engine/core/error.hpp"
#include "engine/core/rid.hpp"
#include "engine/core/scene_types.hpp"
#include "engine/function/scene/structural_diff.hpp"
#include "engine/function/world/world.hpp"
#include "runtime/application.hpp"
#include <algorithm>

namespace gneiss::runtime {
gneiss::engine::error
prefab_placement::capture(runtime::application& application, std::string_view uuid,
                          gneiss::scene_instance_id scene,
                          std::shared_ptr<engine::scene_subtree_snapshot>& snapshot) noexcept try {
  auto* host = &application;
  const auto validation = host->is_owner_thread() && host->accepts_requests()
                              ? gneiss::engine::error::success
                              : gneiss::engine::error::invalid_state;
  if (validation != gneiss::engine::error::success) {
    return validation;
  }
  scene_node_id root;
  auto status = host->scenes()->find_node(scene, uuid, root);
  if (status.failed())
    return status;
  auto candidate = std::make_shared<engine::scene_subtree_snapshot>();
  status = host->scenes()->capture_subtree(scene, root, *candidate);
  if (status.failed())
    return status;
  snapshot = std::move(candidate);
  return engine::error::success;
} catch (const std::bad_alloc&) {
  return gneiss::engine::error::out_of_memory;
} catch (...) {
  return gneiss::engine::error::internal;
}

gneiss::engine::error prefab_placement::freeze(
    runtime::application& application, gneiss::engine::prepared_prefab_description prepared,
    gneiss::engine::object_description author, std::vector<gneiss::engine::render_asset_ref> assets,
    std::shared_ptr<const engine::scene_subtree_snapshot>& output) noexcept try {
  auto* host = &application;
  const auto validation = host->is_owner_thread() && host->accepts_requests()
                              ? gneiss::engine::error::success
                              : gneiss::engine::error::invalid_state;
  if (validation != gneiss::engine::error::success) {
    return validation;
  }
  auto value = std::make_shared<engine::scene_subtree_snapshot>();
  if (!prepared.resource || !author.prefab)
    return engine::error::invalid_argument;
  value->root_uuid = author.uuid;
  value->parent_uuid = author.parent_uuid.value_or("");
  author.parent_uuid.reset();
  value->description.objects.push_back(std::move(author));
  auto& binding = value->resources[value->root_uuid];
  binding.prefab = std::move(prepared.resource);
  binding.source_nodes.resize(binding.prefab->objects.size());
  const auto find = [&](std::string_view uri) {
    const auto entry = std::ranges::find_if(
        assets, [&](const auto& asset) { return engine::asset_uri(asset) == uri; });
    return entry == assets.end() ? engine::render_asset_ref{} : *entry;
  };
  for (std::size_t i = 0; i < binding.source_nodes.size(); ++i) {
    const auto& object = binding.prefab->objects[i];
    if (object.mesh_renderer)
      binding.source_nodes[i] = {
          engine::asset_cast<engine::mesh_resource>(find(object.mesh_renderer->mesh_uri)),
          engine::asset_cast<engine::material_resource>(find(object.mesh_renderer->material_uri))};
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
  if (removing) {
    if (replacing)
      return error::invalid_argument;
    scene_node_id root;
    auto status = app.scenes()->find_node(scene, snapshot.root_uuid, root);
    if (status.failed())
      return status;
    scene_subtree_snapshot current;
    status = app.scenes()->capture_subtree(scene, root, current);
    if (status.failed())
      return status;
    // 历史之外新增的对象不在快照中，不能随旧命令静默删除。
    if (current.description.objects.size() != snapshot.description.objects.size())
      return error::invalid_state;
    for (const auto& object : current.description.objects)
      if (std::ranges::find(snapshot.description.objects, object.uuid, &object_description::uuid) ==
          snapshot.description.objects.end())
        return error::invalid_state;
    return app.scenes()->destroy_subtree(scene, root);
  }
  if (replacing) {
    const auto root = std::ranges::find(snapshot.description.objects, snapshot.root_uuid,
                                        &object_description::uuid);
    const auto resources = snapshot.resources.find(snapshot.root_uuid);
    if (root == snapshot.description.objects.end() || resources == snapshot.resources.end())
      return error::invalid_argument;
    auto author = *root;
    if (!snapshot.parent_uuid.empty())
      author.parent_uuid = snapshot.parent_uuid;
    return app.scenes()->instantiate_prefab_ready(scene, author, resources->second.prefab,
                                                  resources->second.source_nodes, true, output);
  }
  scene_node_id parent;
  if (!snapshot.parent_uuid.empty()) {
    const auto status = app.scenes()->find_node(scene, snapshot.parent_uuid, parent);
    if (status.failed())
      return status;
  }
  return app.scenes()->restore_subtree(scene, snapshot, parent, {}, output);
} catch (const std::bad_alloc&) {
  return gneiss::engine::error::out_of_memory;
} catch (...) {
  return gneiss::engine::error::internal;
}
}
