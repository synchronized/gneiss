// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "scene/prefab_runtime_instance.h"

#include <algorithm>
#include <new>
#include <unordered_map>

namespace {

gneiss_transform to_transform(const gneiss::scene_internal::object_description& object) noexcept {
  gneiss_transform value{};
  std::ranges::copy(object.translation, value.translation);
  std::ranges::copy(object.rotation, value.rotation);
  std::ranges::copy(object.scale, value.scale);
  return value;
}

} // namespace

namespace gneiss::scene_internal {

prefab_runtime_instance::prefab_runtime_instance(gneiss_world world,
                                                 render_internal::render_asset_loader& loader,
                                                 prefab_asset_lease prefab,
                                                 std::string instance_uuid) noexcept
    : world_(world), loader_(loader), prefab_(std::move(prefab)),
      instance_uuid_(std::move(instance_uuid)) {}

prefab_runtime_instance::~prefab_runtime_instance() noexcept { rollback(); }

gneiss_result
prefab_runtime_instance::create(gneiss_world world, render_internal::render_asset_loader& loader,
                                prefab_asset_lease prefab, std::string_view instance_uuid,
                                gneiss_scene_node_id parent, const gneiss_transform& root_transform,
                                std::unique_ptr<prefab_runtime_instance>& out_instance) noexcept {
  out_instance.reset();
  if (world == GNEISS_NULL_WORLD || !prefab || prefab.get()->objects.empty()) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    if (!is_valid_prefab_author_address(
            {std::string(instance_uuid), prefab.get()->objects[0].uuid})) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    auto instance = std::make_unique<prefab_runtime_instance>(world, loader, std::move(prefab),
                                                              std::string(instance_uuid));
    auto result = instance->stage_assets(*instance->prefab_.get());
    if (result == GNEISS_SUCCESS) {
      result = instance->commit(*instance->prefab_.get(), parent, root_transform);
    }
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    out_instance = std::move(instance);
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result prefab_runtime_instance::stage_assets(const prefab_description& description) {
  nodes_.reserve(description.objects.size());
  for (const auto& source : description.objects) {
    runtime_node target{};
    target.address = {instance_uuid_, source.uuid};
    target.name = source.name;
    if (source.mesh_renderer) {
      render_internal::asset_diagnostic diagnostic;
      auto result = loader_.acquire_mesh(source.mesh_renderer->mesh_uri, target.mesh, diagnostic);
      if (result == GNEISS_SUCCESS) {
        result = loader_.acquire_material(source.mesh_renderer->material_uri, target.material,
                                          diagnostic);
      }
      if (result != GNEISS_SUCCESS) {
        return result;
      }
    }
    nodes_.push_back(std::move(target));
  }
  return GNEISS_SUCCESS;
}

gneiss_result prefab_runtime_instance::commit(const prefab_description& description,
                                              gneiss_scene_node_id parent,
                                              const gneiss_transform& root_transform) {
  auto result = gneiss_world_entity_create(world_, &root_entity_);
  if (result == GNEISS_SUCCESS) {
    result = gneiss_scene_node_create(world_, parent, root_entity_, &root_node_);
  }
  if (result == GNEISS_SUCCESS) {
    result = gneiss_scene_node_set_local_transform(world_, root_node_, &root_transform);
  }
  if (result != GNEISS_SUCCESS) {
    return result;
  }

  std::unordered_map<std::string_view, gneiss_scene_node_id> committed_nodes;
  committed_nodes.reserve(description.objects.size());
  std::vector<bool> committed(description.objects.size());
  std::size_t committed_count = 0;
  while (committed_count < description.objects.size()) {
    bool made_progress = false;
    for (std::size_t index = 0; index < description.objects.size(); ++index) {
      if (committed[index]) {
        continue;
      }
      const auto& source = description.objects[index];
      auto parent_node = root_node_;
      if (source.parent_uuid) {
        const auto found = committed_nodes.find(*source.parent_uuid);
        if (found == committed_nodes.end()) {
          continue;
        }
        parent_node = found->second;
      }
      auto& target = nodes_[index];
      result = gneiss_world_entity_create(world_, &target.entity);
      if (result == GNEISS_SUCCESS) {
        result = gneiss_scene_node_create(world_, parent_node, target.entity, &target.node);
      }
      const auto transform = to_transform(source);
      if (result == GNEISS_SUCCESS) {
        result = gneiss_scene_node_set_local_transform(world_, target.node, &transform);
      }
      if (result == GNEISS_SUCCESS && source.camera) {
        const gneiss_camera_desc camera{.struct_size = sizeof(gneiss_camera_desc),
                                        .reserved = 0U,
                                        .vertical_field_of_view_radians =
                                            source.camera->vertical_field_of_view_radians,
                                        .near_plane = source.camera->near_plane,
                                        .far_plane = source.camera->far_plane};
        result = gneiss_world_entity_configure_camera(world_, target.entity, &camera);
        if (result == GNEISS_SUCCESS && source.camera->is_primary) {
          result = gneiss_world_set_active_camera(world_, target.entity);
        }
      }
      if (result == GNEISS_SUCCESS && source.mesh_renderer) {
        const gneiss_mesh_renderer renderer{.mesh = target.mesh.get(),
                                            .material = target.material.get()};
        result = gneiss_world_entity_set_mesh_renderer(world_, target.entity, &renderer);
      }
      if (result != GNEISS_SUCCESS) {
        return result;
      }
      committed_nodes.emplace(source.uuid, target.node);
      committed[index] = true;
      ++committed_count;
      made_progress = true;
    }
    if (!made_progress) {
      return GNEISS_ERROR_INTERNAL;
    }
  }
  return GNEISS_SUCCESS;
}

void prefab_runtime_instance::rollback() noexcept {
  for (auto iterator = nodes_.rbegin(); iterator != nodes_.rend(); ++iterator) {
    if (iterator->entity != GNEISS_NULL_ENTITY_ID) {
      (void)gneiss_world_entity_destroy(world_, iterator->entity);
    }
    if (iterator->node != GNEISS_NULL_SCENE_NODE_ID) {
      (void)gneiss_scene_node_destroy(world_, iterator->node);
    }
    iterator->mesh = {};
    iterator->material = {};
  }
  nodes_.clear();
  if (root_entity_ != GNEISS_NULL_ENTITY_ID) {
    (void)gneiss_world_entity_destroy(world_, root_entity_);
    root_entity_ = GNEISS_NULL_ENTITY_ID;
  }
  if (root_node_ != GNEISS_NULL_SCENE_NODE_ID) {
    (void)gneiss_scene_node_destroy(world_, root_node_);
    root_node_ = GNEISS_NULL_SCENE_NODE_ID;
  }
  prefab_ = {};
  loader_.release_unused();
}

gneiss_scene_node_id
prefab_runtime_instance::find_node(std::string_view source_node_uuid) const noexcept {
  const auto found = std::ranges::find(nodes_, source_node_uuid, [](const runtime_node& node) {
    return std::string_view(node.address.source_node_uuid);
  });
  return found == nodes_.end() ? GNEISS_NULL_SCENE_NODE_ID : found->node;
}

gneiss_result prefab_runtime_instance::get_node_info(std::size_t index,
                                                     node_info& out_info) const noexcept {
  out_info = {};
  if (index >= nodes_.size()) {
    return GNEISS_ERROR_NOT_FOUND;
  }
  const auto& source = nodes_[index];
  gneiss_scene_node_id parent = GNEISS_NULL_SCENE_NODE_ID;
  const auto result = gneiss_scene_node_get_parent(world_, source.node, &parent);
  if (result != GNEISS_SUCCESS) {
    return result;
  }
  out_info = {.address = &source.address,
              .name = source.name,
              .entity = source.entity,
              .node = source.node,
              .parent = parent};
  return GNEISS_SUCCESS;
}

} // namespace gneiss::scene_internal
