// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "scene/scene_instance_service.h"

#include "asset/virtual_file_system.h"
#include "scene/scene_description.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>
#include <new>
#include <unordered_map>

namespace gneiss::scene_internal {
namespace {

std::uint16_t allocate_domain() noexcept {
  static std::atomic_uint32_t next_domain{UINT32_C(1024)};
  const auto value = next_domain.fetch_add(1U, std::memory_order_relaxed);
  return value <= std::numeric_limits<std::uint16_t>::max() ? static_cast<std::uint16_t>(value)
                                                            : UINT16_C(0);
}

gneiss_transform to_transform(const object_description& object) noexcept {
  gneiss_transform value{};
  std::ranges::copy(object.translation, value.translation);
  std::ranges::copy(object.rotation, value.rotation);
  std::ranges::copy(object.scale, value.scale);
  return value;
}

gneiss_result stage_assets(const scene_description& description,
                           render_internal::render_asset_loader& loader, scene_instance& instance) {
  instance.objects.reserve(description.objects.size());
  for (const auto& source : description.objects) {
    scene_instance::object target{};
    target.uuid = source.uuid;
    if (source.mesh_renderer) {
      render_internal::asset_diagnostic diagnostic;
      auto result = loader.acquire_mesh(source.mesh_renderer->mesh_uri, target.mesh, diagnostic);
      if (result == GNEISS_SUCCESS) {
        result = loader.acquire_material(source.mesh_renderer->material_uri, target.material,
                                         diagnostic);
      }
      if (result != GNEISS_SUCCESS) {
        return result;
      }
    }
    instance.objects.push_back(std::move(target));
  }
  return GNEISS_SUCCESS;
}

gneiss_result commit_object(gneiss_world world, const object_description& source,
                            scene_instance::object& target, gneiss_scene_node_id parent) {
  auto result = gneiss_world_entity_create(world, &target.entity);
  if (result == GNEISS_SUCCESS) {
    result = gneiss_scene_node_create(world, parent, target.entity, &target.node);
  }
  const auto transform = to_transform(source);
  if (result == GNEISS_SUCCESS) {
    result = gneiss_scene_node_set_local_transform(world, target.node, &transform);
  }
  if (result == GNEISS_SUCCESS && source.camera) {
    const gneiss_camera_desc camera{.struct_size = sizeof(gneiss_camera_desc),
                                    .reserved = 0U,
                                    .vertical_field_of_view_radians =
                                        source.camera->vertical_field_of_view_radians,
                                    .near_plane = source.camera->near_plane,
                                    .far_plane = source.camera->far_plane};
    result = gneiss_world_entity_configure_camera(world, target.entity, &camera);
    if (result == GNEISS_SUCCESS && source.camera->is_primary) {
      result = gneiss_world_set_active_camera(world, target.entity);
    }
  }
  if (result == GNEISS_SUCCESS && source.mesh_renderer) {
    const gneiss_mesh_renderer renderer{.mesh = target.mesh.get(),
                                        .material = target.material.get()};
    result = gneiss_world_entity_set_mesh_renderer(world, target.entity, &renderer);
  }
  return result;
}

gneiss_result commit_scene(gneiss_world world, const scene_description& description,
                           scene_instance& instance) {
  std::unordered_map<std::string_view, gneiss_scene_node_id> nodes;
  nodes.reserve(description.objects.size());
  std::vector<bool> committed(description.objects.size());
  std::size_t committed_count = 0;
  while (committed_count < description.objects.size()) {
    bool made_progress = false;
    for (std::size_t index = 0; index < description.objects.size(); ++index) {
      if (committed[index]) {
        continue;
      }
      const auto& source = description.objects[index];
      gneiss_scene_node_id parent = GNEISS_NULL_SCENE_NODE_ID;
      if (source.parent_uuid) {
        const auto found = nodes.find(*source.parent_uuid);
        if (found == nodes.end()) {
          continue;
        }
        parent = found->second;
      }
      auto& target = instance.objects[index];
      const auto result = commit_object(world, source, target, parent);
      if (result != GNEISS_SUCCESS) {
        return result;
      }
      nodes.emplace(target.uuid, target.node);
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

} // namespace

scene_instance::scene_instance(gneiss_world world,
                               render_internal::render_asset_loader& loader) noexcept
    : world_(world), loader_(loader) {}

scene_instance::~scene_instance() noexcept { rollback(); }

void scene_instance::rollback() noexcept {
  for (auto iterator = objects.rbegin(); iterator != objects.rend(); ++iterator) {
    if (iterator->entity != GNEISS_NULL_ENTITY_ID) {
      (void)gneiss_world_entity_destroy(world_, iterator->entity);
      iterator->entity = GNEISS_NULL_ENTITY_ID;
    }
    if (iterator->node != GNEISS_NULL_SCENE_NODE_ID) {
      (void)gneiss_scene_node_destroy(world_, iterator->node);
      iterator->node = GNEISS_NULL_SCENE_NODE_ID;
    }
    iterator->mesh = {};
    iterator->material = {};
  }
  objects.clear();
  loader_.release_unused();
}

gneiss_scene_node_id scene_instance::find_node(std::string_view uuid) const noexcept {
  const auto found = std::ranges::find(objects, uuid, &object::uuid);
  return found == objects.end() ? GNEISS_NULL_SCENE_NODE_ID : found->node;
}

gneiss_result scene_instance::serialize(std::string& out_json) const {
  auto current = description;
  gneiss_entity_id active_camera = GNEISS_NULL_ENTITY_ID;
  const auto active_result = gneiss_world_get_active_camera(world_, &active_camera);
  if (active_result != GNEISS_SUCCESS && active_result != GNEISS_ERROR_NOT_READY) {
    return active_result;
  }
  for (std::size_t index = 0; index < objects.size(); ++index) {
    const auto& runtime = objects[index];
    auto& author = current.objects[index];
    gneiss_transform transform = GNEISS_TRANSFORM_IDENTITY;
    auto result = gneiss_world_entity_get_local_transform(world_, runtime.entity, &transform);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    std::ranges::copy(transform.translation, author.translation.begin());
    std::ranges::copy(transform.rotation, author.rotation.begin());
    std::ranges::copy(transform.scale, author.scale.begin());
    if (author.camera) {
      gneiss_camera_desc camera = GNEISS_CAMERA_DESC_INIT;
      result = gneiss_world_entity_get_camera(world_, runtime.entity, &camera);
      if (result != GNEISS_SUCCESS) {
        return result;
      }
      author.camera->vertical_field_of_view_radians = camera.vertical_field_of_view_radians;
      author.camera->near_plane = camera.near_plane;
      author.camera->far_plane = camera.far_plane;
      author.camera->is_primary = runtime.entity == active_camera;
    }
  }
  return serialize_scene_description(current, out_json);
}

scene_instance_service::scene_instance_service(
    gneiss_world world, const asset_internal::virtual_file_system& file_system,
    render_internal::render_asset_loader& loader) noexcept
    : world_(world), file_system_(file_system), loader_(loader), domain_(allocate_domain()),
      instances_(domain_) {}

gneiss_result scene_instance_service::load(std::string_view uri,
                                           gneiss_scene_instance* out_instance) noexcept {
  if (out_instance == nullptr || !is_valid()) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_instance = GNEISS_NULL_SCENE_INSTANCE;
  try {
    scene_description description;
    scene_diagnostic diagnostic;
    auto result = load_scene_description(file_system_, uri, description, diagnostic);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    auto instance = std::make_unique<scene_instance>(world_, loader_);
    result = stage_assets(description, loader_, *instance);
    if (result == GNEISS_SUCCESS) {
      result = commit_scene(world_, description, *instance);
    }
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    instance->description = std::move(description);
    return instances_.create(core::resource_type::scene_instance, std::move(instance),
                             out_instance);
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result scene_instance_service::serialize(gneiss_scene_instance instance,
                                                std::string& out_json) const noexcept {
  try {
    const auto* value = instances_.get(instance, core::resource_type::scene_instance);
    if (value == nullptr || *value == nullptr) {
      return GNEISS_ERROR_INVALID_HANDLE;
    }
    return (*value)->serialize(out_json);
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result scene_instance_service::unload(gneiss_scene_instance instance) noexcept {
  return instances_.destroy(instance, core::resource_type::scene_instance);
}

gneiss_result scene_instance_service::find_node(gneiss_scene_instance instance,
                                                std::string_view uuid,
                                                gneiss_scene_node_id* out_node) const noexcept {
  if (out_node == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_node = GNEISS_NULL_SCENE_NODE_ID;
  const auto* value = instances_.get(instance, core::resource_type::scene_instance);
  if (value == nullptr || *value == nullptr) {
    return GNEISS_ERROR_INVALID_HANDLE;
  }
  *out_node = (*value)->find_node(uuid);
  return *out_node == GNEISS_NULL_SCENE_NODE_ID ? GNEISS_ERROR_NOT_FOUND : GNEISS_SUCCESS;
}

} // namespace gneiss::scene_internal
