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

[[nodiscard]] bool is_canonical_uuid(std::string_view value) noexcept {
  if (value.size() != 36U) {
    return false;
  }
  for (std::size_t index = 0; index < value.size(); ++index) {
    if (index == 8U || index == 13U || index == 18U || index == 23U) {
      if (value[index] != '-') {
        return false;
      }
    } else if ((value[index] < '0' || value[index] > '9') &&
               (value[index] < 'a' || value[index] > 'f')) {
      return false;
    }
  }
  return true;
}

gneiss_result stage_assets(const scene_description& description,
                           render_internal::render_asset_loader& loader, scene_instance& instance) {
  instance.objects.reserve(description.objects.size());
  for (const auto& source : description.objects) {
    scene_instance::object target{};
    target.uuid = source.uuid;
    target.name = source.name;
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

gneiss_result scene_instance::get_node_info(std::uint64_t index,
                                            gneiss_scene_instance_node_info& out_info) const {
  if (index >= objects.size()) {
    return GNEISS_ERROR_NOT_FOUND;
  }
  const auto& object = objects[static_cast<std::size_t>(index)];
  gneiss_entity_id entity = GNEISS_NULL_ENTITY_ID;
  auto result = gneiss_scene_node_get_entity(world_, object.node, &entity);
  if (result != GNEISS_SUCCESS || entity == GNEISS_NULL_ENTITY_ID) {
    return result == GNEISS_SUCCESS ? GNEISS_ERROR_INVALID_HANDLE : result;
  }
  gneiss_scene_node_id parent = GNEISS_NULL_SCENE_NODE_ID;
  result = gneiss_scene_node_get_parent(world_, object.node, &parent);
  if (result != GNEISS_SUCCESS) {
    return result;
  }
  out_info.node = object.node;
  out_info.parent = parent;
  out_info.entity = entity;
  out_info.uuid = object.uuid.data();
  out_info.uuid_length = object.uuid.size();
  out_info.name = object.name.empty() ? nullptr : object.name.data();
  out_info.name_length = object.name.size();
  if (out_info.struct_size >= GNEISS_SCENE_INSTANCE_NODE_INFO_VERSION_2_SIZE) {
    const auto& author = description.objects[static_cast<std::size_t>(index)];
    out_info.mesh_uri = author.mesh_renderer ? author.mesh_renderer->mesh_uri.data() : nullptr;
    out_info.mesh_uri_length =
        author.mesh_renderer ? author.mesh_renderer->mesh_uri.size() : UINT64_C(0);
    out_info.material_uri =
        author.mesh_renderer ? author.mesh_renderer->material_uri.data() : nullptr;
    out_info.material_uri_length =
        author.mesh_renderer ? author.mesh_renderer->material_uri.size() : UINT64_C(0);
  }
  return GNEISS_SUCCESS;
}

gneiss_result
scene_instance::create_mesh_renderer_node(const gneiss_scene_mesh_renderer_node_desc& desc,
                                          gneiss_scene_node_id* out_node) {
  const std::string_view uuid(desc.uuid, static_cast<std::size_t>(desc.uuid_length));
  const std::string_view name(desc.name == nullptr ? "" : desc.name,
                              static_cast<std::size_t>(desc.name_length));
  const std::string_view mesh_uri(desc.renderer.mesh_uri,
                                  static_cast<std::size_t>(desc.renderer.mesh_uri_length));
  const std::string_view material_uri(desc.renderer.material_uri,
                                      static_cast<std::size_t>(desc.renderer.material_uri_length));
  if (!is_canonical_uuid(uuid) || find_node(uuid) != GNEISS_NULL_SCENE_NODE_ID) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  std::optional<std::string> parent_uuid;
  if (desc.parent != GNEISS_NULL_SCENE_NODE_ID) {
    const auto parent = std::ranges::find(objects, desc.parent, &object::node);
    if (parent == objects.end()) {
      return GNEISS_ERROR_INVALID_HANDLE;
    }
    parent_uuid = parent->uuid;
  }

  try {
    object_description author{
        .uuid = std::string(uuid),
        .name = std::string(name),
        .parent_uuid = std::move(parent_uuid),
        .translation = {},
        .rotation = {0.0F, 0.0F, 0.0F, 1.0F},
        .scale = {1.0F, 1.0F, 1.0F},
        .camera = std::nullopt,
        .mesh_renderer = mesh_renderer_description{.mesh_uri = std::string(mesh_uri),
                                                   .material_uri = std::string(material_uri)}};
    objects.reserve(objects.size() + 1U);
    description.objects.reserve(description.objects.size() + 1U);
    object target{};
    target.uuid = author.uuid;
    target.name = author.name;
    render_internal::asset_diagnostic diagnostic;
    auto result = loader_.acquire_mesh(mesh_uri, target.mesh, diagnostic);
    if (result == GNEISS_SUCCESS) {
      result = loader_.acquire_material(material_uri, target.material, diagnostic);
    }
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    result = commit_object(world_, author, target, desc.parent);
    if (result != GNEISS_SUCCESS) {
      if (target.entity != GNEISS_NULL_ENTITY_ID) {
        (void)gneiss_world_entity_destroy(world_, target.entity);
      }
      if (target.node != GNEISS_NULL_SCENE_NODE_ID) {
        (void)gneiss_scene_node_destroy(world_, target.node);
      }
      return result;
    }
    *out_node = target.node;
    objects.push_back(std::move(target));
    description.objects.push_back(std::move(author));
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result scene_instance::set_mesh_renderer(gneiss_scene_node_id node,
                                                std::string_view mesh_uri,
                                                std::string_view material_uri) {
  const auto found = std::ranges::find(objects, node, &object::node);
  if (found == objects.end()) {
    return GNEISS_ERROR_INVALID_HANDLE;
  }
  try {
    mesh_renderer_description author{.mesh_uri = std::string(mesh_uri),
                                     .material_uri = std::string(material_uri)};
    render_internal::mesh_asset_lease mesh;
    render_internal::material_asset_lease material;
    render_internal::asset_diagnostic diagnostic;
    auto result = loader_.acquire_mesh(mesh_uri, mesh, diagnostic);
    if (result == GNEISS_SUCCESS) {
      result = loader_.acquire_material(material_uri, material, diagnostic);
    }
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    const gneiss_mesh_renderer renderer{.mesh = mesh.get(), .material = material.get()};
    result = gneiss_world_entity_set_mesh_renderer(world_, found->entity, &renderer);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    const auto index = static_cast<std::size_t>(std::distance(objects.begin(), found));
    description.objects[index].mesh_renderer = std::move(author);
    found->mesh = std::move(mesh);
    found->material = std::move(material);
    loader_.release_unused();
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result scene_instance::destroy_node(gneiss_scene_node_id node) {
  const auto found = std::ranges::find(objects, node, &object::node);
  if (found == objects.end()) {
    return GNEISS_ERROR_INVALID_HANDLE;
  }
  const auto index = static_cast<std::size_t>(std::distance(objects.begin(), found));
  const auto& uuid = description.objects[index].uuid;
  if (std::ranges::any_of(description.objects, [&uuid](const auto& candidate) {
        return candidate.parent_uuid && *candidate.parent_uuid == uuid;
      })) {
    return GNEISS_ERROR_INVALID_STATE;
  }
  auto result = gneiss_world_entity_destroy(world_, found->entity);
  if (result != GNEISS_SUCCESS) {
    return result;
  }
  result = gneiss_scene_node_destroy(world_, found->node);
  if (result != GNEISS_SUCCESS && result != GNEISS_ERROR_INVALID_HANDLE) {
    return result;
  }
  objects.erase(found);
  description.objects.erase(description.objects.begin() + static_cast<std::ptrdiff_t>(index));
  loader_.release_unused();
  return GNEISS_SUCCESS;
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

gneiss_result scene_instance_service::get_node_count(gneiss_scene_instance instance,
                                                     std::uint64_t* out_count) const noexcept {
  if (out_count == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_count = 0U;
  const auto* value = instances_.get(instance, core::resource_type::scene_instance);
  if (value == nullptr || *value == nullptr) {
    return GNEISS_ERROR_INVALID_HANDLE;
  }
  *out_count = (*value)->objects.size();
  return GNEISS_SUCCESS;
}

gneiss_result
scene_instance_service::get_node_info(gneiss_scene_instance instance, std::uint64_t index,
                                      gneiss_scene_instance_node_info* out_info) const noexcept {
  if (out_info == nullptr ||
      out_info->struct_size < GNEISS_SCENE_INSTANCE_NODE_INFO_VERSION_1_SIZE) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  const auto struct_size = out_info->struct_size;
  out_info->reserved = 0U;
  out_info->node = GNEISS_NULL_SCENE_NODE_ID;
  out_info->parent = GNEISS_NULL_SCENE_NODE_ID;
  out_info->entity = GNEISS_NULL_ENTITY_ID;
  out_info->uuid = nullptr;
  out_info->uuid_length = 0U;
  out_info->name = nullptr;
  out_info->name_length = 0U;
  out_info->reserved_2[0] = 0U;
  out_info->reserved_2[1] = 0U;
  if (struct_size >= GNEISS_SCENE_INSTANCE_NODE_INFO_VERSION_2_SIZE) {
    out_info->mesh_uri = nullptr;
    out_info->mesh_uri_length = 0U;
    out_info->material_uri = nullptr;
    out_info->material_uri_length = 0U;
  }
  out_info->struct_size = struct_size;
  const auto* value = instances_.get(instance, core::resource_type::scene_instance);
  if (value == nullptr || *value == nullptr) {
    return GNEISS_ERROR_INVALID_HANDLE;
  }
  return (*value)->get_node_info(index, *out_info);
}

gneiss_result
scene_instance_service::create_mesh_renderer_node(gneiss_scene_instance instance,
                                                  const gneiss_scene_mesh_renderer_node_desc& desc,
                                                  gneiss_scene_node_id* out_node) noexcept {
  if (out_node == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_node = GNEISS_NULL_SCENE_NODE_ID;
  try {
    auto* value = instances_.get(instance, core::resource_type::scene_instance);
    return value == nullptr || *value == nullptr
               ? GNEISS_ERROR_INVALID_HANDLE
               : (*value)->create_mesh_renderer_node(desc, out_node);
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result scene_instance_service::set_mesh_renderer(gneiss_scene_instance instance,
                                                        gneiss_scene_node_id node,
                                                        std::string_view mesh_uri,
                                                        std::string_view material_uri) noexcept {
  try {
    auto* value = instances_.get(instance, core::resource_type::scene_instance);
    return value == nullptr || *value == nullptr
               ? GNEISS_ERROR_INVALID_HANDLE
               : (*value)->set_mesh_renderer(node, mesh_uri, material_uri);
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result scene_instance_service::destroy_node(gneiss_scene_instance instance,
                                                   gneiss_scene_node_id node) noexcept {
  try {
    auto* value = instances_.get(instance, core::resource_type::scene_instance);
    return value == nullptr || *value == nullptr ? GNEISS_ERROR_INVALID_HANDLE
                                                 : (*value)->destroy_node(node);
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

} // namespace gneiss::scene_internal
