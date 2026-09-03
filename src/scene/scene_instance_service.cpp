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
#include <unordered_set>

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

gneiss_transform to_transform(const prefab_instance_description& instance) noexcept {
  gneiss_transform value{};
  std::ranges::copy(instance.translation, value.translation);
  std::ranges::copy(instance.rotation, value.rotation);
  std::ranges::copy(instance.scale, value.scale);
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

gneiss_result commit_prefab_instances(gneiss_world world, const scene_description& description,
                                      prefab_asset_loader& prefab_loader,
                                      render_internal::render_asset_loader& render_loader,
                                      scene_instance& instance) {
  instance.prefab_instances.reserve(description.prefab_instances.size());
  for (const auto& source : description.prefab_instances) {
    prefab_asset_lease lease;
    scene_diagnostic diagnostic;
    auto result = prefab_loader.acquire(source.prefab_uri, lease, diagnostic);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    const auto parent =
        source.parent_uuid ? instance.find_node(*source.parent_uuid) : GNEISS_NULL_SCENE_NODE_ID;
    if (source.parent_uuid && parent == GNEISS_NULL_SCENE_NODE_ID) {
      return GNEISS_ERROR_INTERNAL;
    }
    std::unique_ptr<prefab_runtime_instance> runtime;
    result = prefab_runtime_instance::create(world, render_loader, std::move(lease),
                                             source.instance_uuid, parent, to_transform(source),
                                             runtime);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    instance.prefab_instances.push_back(std::move(runtime));
  }
  return GNEISS_SUCCESS;
}

} // namespace

scene_instance::scene_instance(gneiss_world world, render_internal::render_asset_loader& loader,
                               prefab_asset_loader& prefab_loader) noexcept
    : world_(world), loader_(loader), prefab_loader_(prefab_loader) {}

scene_instance::~scene_instance() noexcept { rollback(); }

void scene_instance::rollback() noexcept {
  prefab_instances.clear();
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
  if (out_info.struct_size >= GNEISS_SCENE_INSTANCE_NODE_INFO_VERSION_3_SIZE) {
    const auto& author = description.objects[static_cast<std::size_t>(index)];
    result = gneiss_world_entity_get_local_transform(world_, entity, &out_info.local_transform);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    out_info.component_flags = author.mesh_renderer != std::nullopt
                                   ? GNEISS_SCENE_NODE_COMPONENT_MESH_RENDERER
                                   : UINT32_C(0);
    if (author.camera) {
      result = gneiss_world_entity_get_camera(world_, entity, &out_info.camera);
      if (result != GNEISS_SUCCESS) {
        return result;
      }
      out_info.component_flags |= GNEISS_SCENE_NODE_COMPONENT_CAMERA;
      if (author.camera->is_primary) {
        out_info.component_flags |= GNEISS_SCENE_NODE_COMPONENT_PRIMARY_CAMERA;
      }
    }
  }
  return GNEISS_SUCCESS;
}

std::uint64_t scene_instance::get_prefab_node_count() const noexcept {
  std::uint64_t count = prefab_instances.size();
  for (const auto& instance : prefab_instances) {
    count += instance->node_count();
  }
  return count;
}

gneiss_result scene_instance::get_prefab_node_info(std::uint64_t index,
                                                   gneiss_scene_prefab_node_info& out_info) const {
  std::uint64_t offset = 0;
  for (std::size_t instance_index = 0; instance_index < prefab_instances.size(); ++instance_index) {
    const auto& runtime = *prefab_instances[instance_index];
    const auto& author = description.prefab_instances[instance_index];
    if (index == offset) {
      gneiss_scene_node_id parent = GNEISS_NULL_SCENE_NODE_ID;
      auto result = gneiss_scene_node_get_parent(world_, runtime.root(), &parent);
      if (result != GNEISS_SUCCESS) {
        return result;
      }
      gneiss_transform transform = GNEISS_TRANSFORM_IDENTITY;
      result = gneiss_scene_node_get_local_transform(world_, runtime.root(), &transform);
      if (result != GNEISS_SUCCESS) {
        return result;
      }
      out_info.flags = GNEISS_SCENE_PREFAB_NODE_INSTANCE_ROOT;
      out_info.node = runtime.root();
      out_info.parent = parent;
      out_info.entity = runtime.root_entity();
      out_info.instance_uuid = author.instance_uuid.data();
      out_info.instance_uuid_length = author.instance_uuid.size();
      out_info.name = author.name.empty() ? nullptr : author.name.data();
      out_info.name_length = author.name.size();
      out_info.prefab_uri = author.prefab_uri.data();
      out_info.prefab_uri_length = author.prefab_uri.size();
      out_info.local_transform = transform;
      return GNEISS_SUCCESS;
    }
    ++offset;
    if (index < offset + runtime.node_count()) {
      prefab_runtime_instance::node_info source;
      const auto result = runtime.get_node_info(static_cast<std::size_t>(index - offset), source);
      if (result != GNEISS_SUCCESS) {
        return result;
      }
      out_info.flags = GNEISS_SCENE_PREFAB_NODE_SOURCE_READ_ONLY;
      out_info.node = source.node;
      out_info.parent = source.parent;
      out_info.entity = source.entity;
      out_info.instance_uuid = source.address->instance_uuid.data();
      out_info.instance_uuid_length = source.address->instance_uuid.size();
      out_info.source_node_uuid = source.address->source_node_uuid.data();
      out_info.source_node_uuid_length = source.address->source_node_uuid.size();
      out_info.name = source.name.empty() ? nullptr : source.name.data();
      out_info.name_length = source.name.size();
      out_info.prefab_uri = author.prefab_uri.data();
      out_info.prefab_uri_length = author.prefab_uri.size();
      const auto transform_result =
          gneiss_scene_node_get_local_transform(world_, source.node, &out_info.local_transform);
      return transform_result;
    }
    offset += runtime.node_count();
  }
  return GNEISS_ERROR_NOT_FOUND;
}

gneiss_result scene_instance::create_prefab_instance(const gneiss_scene_prefab_instance_desc& desc,
                                                     gneiss_scene_node_id* out_root) {
  *out_root = GNEISS_NULL_SCENE_NODE_ID;
  const std::string_view instance_uuid{desc.instance_uuid,
                                       static_cast<std::size_t>(desc.instance_uuid_length)};
  const std::string_view name{desc.name == nullptr ? "" : desc.name,
                              static_cast<std::size_t>(desc.name_length)};
  const std::string_view prefab_uri{desc.prefab_uri,
                                    static_cast<std::size_t>(desc.prefab_uri_length)};
  if (!is_canonical_uuid(instance_uuid) ||
      std::ranges::any_of(
          objects, [instance_uuid](const auto& object) { return object.uuid == instance_uuid; }) ||
      std::ranges::any_of(description.prefab_instances, [instance_uuid](const auto& instance) {
        return instance.instance_uuid == instance_uuid;
      })) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    std::optional<std::string> parent_uuid;
    if (desc.parent != GNEISS_NULL_SCENE_NODE_ID) {
      const auto found = std::ranges::find(objects, desc.parent, &object::node);
      if (found == objects.end()) {
        return GNEISS_ERROR_INVALID_HANDLE;
      }
      parent_uuid = found->uuid;
    }
    prefab_asset_lease lease;
    scene_diagnostic diagnostic;
    auto result = prefab_loader_.acquire(prefab_uri, lease, diagnostic);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    prefab_instance_description author{.instance_uuid = std::string(instance_uuid),
                                       .name = std::string(name),
                                       .parent_uuid = std::move(parent_uuid),
                                       .prefab_uri = std::string(prefab_uri)};
    std::ranges::copy(desc.local_transform.translation, author.translation.begin());
    std::ranges::copy(desc.local_transform.rotation, author.rotation.begin());
    std::ranges::copy(desc.local_transform.scale, author.scale.begin());
    description.prefab_instances.reserve(description.prefab_instances.size() + 1U);
    prefab_instances.reserve(prefab_instances.size() + 1U);
    std::unique_ptr<prefab_runtime_instance> runtime;
    result = prefab_runtime_instance::create(world_, loader_, std::move(lease), instance_uuid,
                                             desc.parent, desc.local_transform, runtime);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    *out_root = runtime->root();
    description.prefab_instances.push_back(std::move(author));
    prefab_instances.push_back(std::move(runtime));
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result scene_instance::set_prefab_instance_name(gneiss_scene_node_id root,
                                                       std::string_view name) {
  const auto found = std::ranges::find_if(
      prefab_instances, [root](const auto& instance) { return instance->root() == root; });
  if (found == prefab_instances.end()) {
    return GNEISS_ERROR_INVALID_HANDLE;
  }
  try {
    std::string prepared(name);
    description.prefab_instances[static_cast<std::size_t>(found - prefab_instances.begin())]
        .name.swap(prepared);
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result scene_instance::destroy_prefab_instance(gneiss_scene_node_id root) noexcept {
  const auto found = std::ranges::find_if(
      prefab_instances, [root](const auto& instance) { return instance->root() == root; });
  if (found == prefab_instances.end()) {
    return GNEISS_ERROR_INVALID_HANDLE;
  }
  const auto index = static_cast<std::size_t>(found - prefab_instances.begin());
  prefab_instances.erase(found);
  description.prefab_instances.erase(description.prefab_instances.begin() +
                                     static_cast<std::ptrdiff_t>(index));
  return GNEISS_SUCCESS;
}

gneiss_result
scene_instance::refresh_prefab_instance(gneiss_scene_node_id root,
                                        gneiss_scene_node_id* out_new_root,
                                        gneiss_scene_prefab_refresh_token* out_token) {
  *out_new_root = GNEISS_NULL_SCENE_NODE_ID;
  *out_token = GNEISS_NULL_SCENE_PREFAB_REFRESH_TOKEN;
  const auto found = std::ranges::find_if(
      prefab_instances, [root](const auto& instance) { return instance->root() == root; });
  if (found == prefab_instances.end()) {
    return GNEISS_ERROR_INVALID_HANDLE;
  }
  const auto index = static_cast<std::size_t>(found - prefab_instances.begin());
  const auto& author = description.prefab_instances[index];
  gneiss_scene_node_id parent = GNEISS_NULL_SCENE_NODE_ID;
  auto result = gneiss_scene_node_get_parent(world_, root, &parent);
  gneiss_transform transform = GNEISS_TRANSFORM_IDENTITY;
  if (result == GNEISS_SUCCESS) {
    result = gneiss_scene_node_get_local_transform(world_, root, &transform);
  }
  if (result != GNEISS_SUCCESS) {
    return result;
  }
  if (next_prefab_refresh_token_ == GNEISS_NULL_SCENE_PREFAB_REFRESH_TOKEN) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  }
  prefab_refresh_transactions_.reserve(prefab_refresh_transactions_.size() + 1U);
  auto old_lease = (*found)->prefab_lease();
  prefab_asset_lease lease;
  scene_diagnostic diagnostic;
  result = prefab_loader_.reload(author.prefab_uri, lease, diagnostic);
  if (result != GNEISS_SUCCESS) {
    return result;
  }
  std::unique_ptr<prefab_runtime_instance> replacement;
  result = prefab_runtime_instance::create(world_, loader_, std::move(lease), author.instance_uuid,
                                           parent, transform, replacement);
  if (result != GNEISS_SUCCESS) {
    return result;
  }
  const auto token = next_prefab_refresh_token_++;
  prefab_refresh_transactions_.push_back(
      {.token = token, .instance_uuid = author.instance_uuid, .alternate = std::move(old_lease)});
  *out_new_root = replacement->root();
  *out_token = token;
  *found = std::move(replacement);
  return GNEISS_SUCCESS;
}

gneiss_result scene_instance::toggle_prefab_refresh(gneiss_scene_prefab_refresh_token token,
                                                    gneiss_scene_node_id* out_new_root) {
  *out_new_root = GNEISS_NULL_SCENE_NODE_ID;
  const auto transaction =
      std::ranges::find(prefab_refresh_transactions_, token, &prefab_refresh_transaction::token);
  if (transaction == prefab_refresh_transactions_.end()) {
    return GNEISS_ERROR_INVALID_HANDLE;
  }
  const auto author = std::ranges::find(description.prefab_instances, transaction->instance_uuid,
                                        &prefab_instance_description::instance_uuid);
  if (author == description.prefab_instances.end()) {
    return GNEISS_ERROR_INVALID_STATE;
  }
  const auto index = static_cast<std::size_t>(author - description.prefab_instances.begin());
  if (index >= prefab_instances.size()) {
    return GNEISS_ERROR_INTERNAL;
  }
  auto& current = prefab_instances[index];
  gneiss_scene_node_id parent = GNEISS_NULL_SCENE_NODE_ID;
  auto result = gneiss_scene_node_get_parent(world_, current->root(), &parent);
  gneiss_transform transform = GNEISS_TRANSFORM_IDENTITY;
  if (result == GNEISS_SUCCESS) {
    result = gneiss_scene_node_get_local_transform(world_, current->root(), &transform);
  }
  if (result != GNEISS_SUCCESS) {
    return result;
  }
  auto current_lease = current->prefab_lease();
  std::unique_ptr<prefab_runtime_instance> replacement;
  result = prefab_runtime_instance::create(world_, loader_, transaction->alternate,
                                           author->instance_uuid, parent, transform, replacement);
  if (result != GNEISS_SUCCESS) {
    return result;
  }
  transaction->alternate = std::move(current_lease);
  *out_new_root = replacement->root();
  current = std::move(replacement);
  return GNEISS_SUCCESS;
}

gneiss_result
scene_instance::release_prefab_refresh(gneiss_scene_prefab_refresh_token token) noexcept {
  const auto found =
      std::ranges::find(prefab_refresh_transactions_, token, &prefab_refresh_transaction::token);
  if (found == prefab_refresh_transactions_.end()) {
    return GNEISS_ERROR_INVALID_HANDLE;
  }
  prefab_refresh_transactions_.erase(found);
  return GNEISS_SUCCESS;
}

gneiss_result scene_instance::create_node(const gneiss_scene_node_desc& desc,
                                          gneiss_scene_node_id* out_node) {
  const std::string_view uuid(desc.uuid, static_cast<std::size_t>(desc.uuid_length));
  const std::string_view name(desc.name == nullptr ? "" : desc.name,
                              static_cast<std::size_t>(desc.name_length));
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
    object_description author{.uuid = std::string(uuid),
                              .name = std::string(name),
                              .parent_uuid = std::move(parent_uuid),
                              .translation = {},
                              .rotation = {},
                              .scale = {},
                              .camera = std::nullopt,
                              .mesh_renderer = std::nullopt};
    std::ranges::copy(desc.local_transform.translation, author.translation.begin());
    std::ranges::copy(desc.local_transform.rotation, author.rotation.begin());
    std::ranges::copy(desc.local_transform.scale, author.scale.begin());
    objects.reserve(objects.size() + 1U);
    description.objects.reserve(description.objects.size() + 1U);
    object target{.uuid = author.uuid,
                  .name = author.name,
                  .entity = GNEISS_NULL_ENTITY_ID,
                  .node = GNEISS_NULL_SCENE_NODE_ID,
                  .mesh = {},
                  .material = {}};
    const auto result = commit_object(world_, author, target, desc.parent);
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

gneiss_result scene_instance::set_node_name(gneiss_scene_node_id node, std::string_view name) {
  const auto found = std::ranges::find(objects, node, &object::node);
  if (found == objects.end()) {
    return GNEISS_ERROR_INVALID_HANDLE;
  }
  try {
    std::string runtime_name(name);
    std::string author_name(name);
    const auto index = static_cast<std::size_t>(std::distance(objects.begin(), found));
    found->name.swap(runtime_name);
    description.objects[index].name.swap(author_name);
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  }
}

gneiss_result scene_instance::reparent_node(gneiss_scene_node_id node,
                                            gneiss_scene_node_id parent) {
  const auto found = std::ranges::find(objects, node, &object::node);
  if (found == objects.end()) {
    return GNEISS_ERROR_INVALID_HANDLE;
  }
  std::optional<std::string> parent_uuid;
  try {
    if (parent != GNEISS_NULL_SCENE_NODE_ID) {
      const auto parent_object = std::ranges::find(objects, parent, &object::node);
      if (parent_object == objects.end()) {
        return GNEISS_ERROR_INVALID_HANDLE;
      }
      parent_uuid = parent_object->uuid;
    }
    const auto result = gneiss_scene_node_reparent(world_, node, parent);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    const auto index = static_cast<std::size_t>(std::distance(objects.begin(), found));
    description.objects[index].parent_uuid.swap(parent_uuid);
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  }
}

gneiss_result scene_instance::capture_subtree(gneiss_scene_node_id root,
                                              std::string& out_snapshot) const {
  const auto found = std::ranges::find(objects, root, &object::node);
  if (found == objects.end()) {
    return GNEISS_ERROR_INVALID_HANDLE;
  }
  try {
    std::string current_json;
    auto result = serialize(current_json);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    scene_description current;
    scene_diagnostic diagnostic;
    result = parse_scene_description(current_json, current, diagnostic);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    std::unordered_set<std::string> included{found->uuid};
    bool changed = true;
    while (changed) {
      changed = false;
      for (const auto& candidate : current.objects) {
        if (candidate.parent_uuid && included.contains(*candidate.parent_uuid) &&
            included.emplace(candidate.uuid).second) {
          changed = true;
        }
      }
    }
    if (included.size() > GNEISS_SCENE_SUBTREE_MAX_NODES) {
      return GNEISS_ERROR_UNSUPPORTED;
    }
    std::erase_if(current.objects, [&included](const auto& candidate) {
      return !included.contains(candidate.uuid);
    });
    current.author_json =
        std::string{"{\"format\":\"gneiss.scene\",\"version\":3,\"scene_uuid\":\""} + current.uuid +
        "\",\"objects\":[],\"prefab_instances\":[]}";
    return serialize_scene_description(current, out_snapshot);
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result scene_instance::restore_subtree(std::string_view snapshot,
                                              gneiss_scene_node_id parent,
                                              const gneiss_scene_uuid_mapping* mappings,
                                              std::uint64_t mapping_count,
                                              gneiss_scene_node_id* out_root) {
  scene_description subtree;
  scene_diagnostic diagnostic;
  auto result = parse_scene_description(snapshot, subtree, diagnostic);
  if (result != GNEISS_SUCCESS || subtree.objects.empty()) {
    return result == GNEISS_SUCCESS ? GNEISS_ERROR_INVALID_ARGUMENT : result;
  }
  if (subtree.objects.size() > GNEISS_SCENE_SUBTREE_MAX_NODES) {
    return GNEISS_ERROR_UNSUPPORTED;
  }
  std::optional<std::string> external_parent_uuid;
  if (parent != GNEISS_NULL_SCENE_NODE_ID) {
    const auto found = std::ranges::find(objects, parent, &object::node);
    if (found == objects.end()) {
      return GNEISS_ERROR_INVALID_HANDLE;
    }
    external_parent_uuid = found->uuid;
  }
  try {
    std::unordered_set<std::string> source_uuids;
    source_uuids.reserve(subtree.objects.size());
    for (const auto& author : subtree.objects) {
      source_uuids.emplace(author.uuid);
    }
    auto root = subtree.objects.end();
    for (auto iterator = subtree.objects.begin(); iterator != subtree.objects.end(); ++iterator) {
      if (!iterator->parent_uuid || !source_uuids.contains(*iterator->parent_uuid)) {
        if (root != subtree.objects.end()) {
          return GNEISS_ERROR_INVALID_ARGUMENT;
        }
        root = iterator;
      }
    }
    if (root == subtree.objects.end()) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    const auto source_root_uuid = root->uuid;
    std::unordered_map<std::string, std::string> uuid_map;
    if (mapping_count != 0U) {
      if (mapping_count != subtree.objects.size()) {
        return GNEISS_ERROR_INVALID_ARGUMENT;
      }
      uuid_map.reserve(static_cast<std::size_t>(mapping_count));
      std::unordered_set<std::string> targets;
      targets.reserve(static_cast<std::size_t>(mapping_count));
      for (std::uint64_t index = 0; index < mapping_count; ++index) {
        const auto& mapping = mappings[index];
        const std::string source(mapping.source_uuid,
                                 static_cast<std::size_t>(mapping.source_uuid_length));
        const std::string target(mapping.target_uuid,
                                 static_cast<std::size_t>(mapping.target_uuid_length));
        if (!source_uuids.contains(source) || !is_canonical_uuid(target) ||
            find_node(target) != GNEISS_NULL_SCENE_NODE_ID ||
            !uuid_map.emplace(source, target).second || !targets.emplace(target).second) {
          return GNEISS_ERROR_INVALID_ARGUMENT;
        }
      }
    } else {
      for (const auto& author : subtree.objects) {
        if (find_node(author.uuid) != GNEISS_NULL_SCENE_NODE_ID) {
          return GNEISS_ERROR_INVALID_ARGUMENT;
        }
      }
    }
    for (auto& author : subtree.objects) {
      const auto old_uuid = author.uuid;
      if (!uuid_map.empty()) {
        author.uuid = uuid_map.at(old_uuid);
        if (author.camera) {
          author.camera->is_primary = false;
        }
      }
      if (old_uuid == source_root_uuid) {
        author.parent_uuid = external_parent_uuid;
      } else if (author.parent_uuid && !uuid_map.empty()) {
        author.parent_uuid = uuid_map.at(*author.parent_uuid);
      }
    }

    std::vector<object> staged;
    staged.reserve(subtree.objects.size());
    for (const auto& author : subtree.objects) {
      object target{.uuid = author.uuid,
                    .name = author.name,
                    .entity = GNEISS_NULL_ENTITY_ID,
                    .node = GNEISS_NULL_SCENE_NODE_ID,
                    .mesh = {},
                    .material = {}};
      if (author.mesh_renderer) {
        render_internal::asset_diagnostic asset_diagnostic;
        result =
            loader_.acquire_mesh(author.mesh_renderer->mesh_uri, target.mesh, asset_diagnostic);
        if (result == GNEISS_SUCCESS) {
          result = loader_.acquire_material(author.mesh_renderer->material_uri, target.material,
                                            asset_diagnostic);
        }
        if (result != GNEISS_SUCCESS) {
          return result;
        }
      }
      staged.push_back(std::move(target));
    }
    objects.reserve(objects.size() + staged.size());
    description.objects.reserve(description.objects.size() + subtree.objects.size());
    std::unordered_map<std::string_view, gneiss_scene_node_id> committed_nodes;
    committed_nodes.reserve(staged.size());
    std::vector<bool> committed(staged.size());
    std::size_t committed_count = 0;
    while (committed_count < staged.size()) {
      bool made_progress = false;
      for (std::size_t index = 0; index < staged.size(); ++index) {
        if (committed[index]) {
          continue;
        }
        const auto& author = subtree.objects[index];
        gneiss_scene_node_id runtime_parent = GNEISS_NULL_SCENE_NODE_ID;
        if (author.parent_uuid) {
          if (external_parent_uuid && *author.parent_uuid == *external_parent_uuid) {
            runtime_parent = parent;
          } else {
            const auto found_parent = committed_nodes.find(*author.parent_uuid);
            if (found_parent == committed_nodes.end()) {
              continue;
            }
            runtime_parent = found_parent->second;
          }
        }
        result = commit_object(world_, author, staged[index], runtime_parent);
        if (result != GNEISS_SUCCESS) {
          for (auto& value : staged) {
            if (value.entity != GNEISS_NULL_ENTITY_ID) {
              (void)gneiss_world_entity_destroy(world_, value.entity);
            }
            if (value.node != GNEISS_NULL_SCENE_NODE_ID) {
              (void)gneiss_scene_node_destroy(world_, value.node);
            }
          }
          loader_.release_unused();
          return result;
        }
        committed_nodes.emplace(staged[index].uuid, staged[index].node);
        committed[index] = true;
        ++committed_count;
        made_progress = true;
      }
      if (!made_progress) {
        return GNEISS_ERROR_INVALID_ARGUMENT;
      }
    }
    const auto restored_root_uuid =
        uuid_map.empty() ? source_root_uuid : uuid_map.at(source_root_uuid);
    *out_root = committed_nodes.at(restored_root_uuid);
    for (auto& value : staged) {
      objects.push_back(std::move(value));
    }
    for (auto& author : subtree.objects) {
      description.objects.push_back(std::move(author));
    }
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result scene_instance::destroy_subtree(gneiss_scene_node_id root) {
  const auto found = std::ranges::find(objects, root, &object::node);
  if (found == objects.end()) {
    return GNEISS_ERROR_INVALID_HANDLE;
  }
  try {
    std::unordered_set<std::string> removed{found->uuid};
    bool changed = true;
    while (changed) {
      changed = false;
      for (const auto& author : description.objects) {
        if (author.parent_uuid && removed.contains(*author.parent_uuid) &&
            removed.emplace(author.uuid).second) {
          changed = true;
        }
      }
    }
    std::vector<std::pair<object*, std::size_t>> ordered;
    ordered.reserve(removed.size());
    for (auto& value : objects) {
      if (!removed.contains(value.uuid)) {
        continue;
      }
      if (gneiss_scene_node_get_entity(world_, value.node, &value.entity) != GNEISS_SUCCESS) {
        return GNEISS_ERROR_INVALID_HANDLE;
      }
      std::size_t depth = 0U;
      auto current_uuid = std::string_view{value.uuid};
      while (true) {
        const auto author =
            std::ranges::find(description.objects, current_uuid, &object_description::uuid);
        if (author == description.objects.end() || !author->parent_uuid ||
            !removed.contains(*author->parent_uuid)) {
          break;
        }
        ++depth;
        current_uuid = *author->parent_uuid;
      }
      ordered.emplace_back(&value, depth);
    }
    std::ranges::sort(
        ordered, [](const auto& left, const auto& right) { return left.second > right.second; });
    for (const auto& [value, depth] : ordered) {
      (void)depth;
      auto destroy_result = gneiss_world_entity_destroy(world_, value->entity);
      if (destroy_result != GNEISS_SUCCESS) {
        return destroy_result;
      }
      destroy_result = gneiss_scene_node_destroy(world_, value->node);
      if (destroy_result != GNEISS_SUCCESS) {
        return destroy_result;
      }
    }
    std::erase_if(objects, [&removed](const auto& value) { return removed.contains(value.uuid); });
    std::erase_if(description.objects,
                  [&removed](const auto& value) { return removed.contains(value.uuid); });
    loader_.release_unused();
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  }
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

gneiss_result scene_instance::set_camera(gneiss_scene_node_id node,
                                         const gneiss_scene_camera_desc& desc) {
  const auto found = std::ranges::find(objects, node, &object::node);
  if (found == objects.end()) {
    return GNEISS_ERROR_INVALID_HANDLE;
  }
  const auto result = gneiss_world_entity_configure_camera(world_, found->entity, &desc.camera);
  if (result != GNEISS_SUCCESS) {
    return result;
  }
  if (desc.is_primary != 0U) {
    const auto active_result = gneiss_world_set_active_camera(world_, found->entity);
    if (active_result != GNEISS_SUCCESS) {
      return active_result;
    }
  }
  const auto index = static_cast<std::size_t>(std::distance(objects.begin(), found));
  if (desc.is_primary != 0U) {
    for (auto& author : description.objects) {
      if (author.camera) {
        author.camera->is_primary = false;
      }
    }
  }
  description.objects[index].camera = camera_description{
      .vertical_field_of_view_radians = desc.camera.vertical_field_of_view_radians,
      .near_plane = desc.camera.near_plane,
      .far_plane = desc.camera.far_plane,
      .is_primary = desc.is_primary != 0U};
  return GNEISS_SUCCESS;
}

gneiss_result scene_instance::remove_camera(gneiss_scene_node_id node) {
  const auto found = std::ranges::find(objects, node, &object::node);
  if (found == objects.end()) {
    return GNEISS_ERROR_INVALID_HANDLE;
  }
  const auto index = static_cast<std::size_t>(std::distance(objects.begin(), found));
  if (!description.objects[index].camera) {
    return GNEISS_ERROR_NOT_FOUND;
  }
  const auto result = gneiss_world_entity_remove_camera(world_, found->entity);
  if (result == GNEISS_SUCCESS) {
    description.objects[index].camera.reset();
  }
  return result;
}

gneiss_result scene_instance::remove_mesh_renderer(gneiss_scene_node_id node) {
  const auto found = std::ranges::find(objects, node, &object::node);
  if (found == objects.end()) {
    return GNEISS_ERROR_INVALID_HANDLE;
  }
  const auto index = static_cast<std::size_t>(std::distance(objects.begin(), found));
  if (!description.objects[index].mesh_renderer) {
    return GNEISS_ERROR_NOT_FOUND;
  }
  const auto result = gneiss_world_entity_remove_mesh_renderer(world_, found->entity);
  if (result != GNEISS_SUCCESS) {
    return result;
  }
  description.objects[index].mesh_renderer.reset();
  found->mesh = {};
  found->material = {};
  loader_.release_unused();
  return GNEISS_SUCCESS;
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
  if (prefab_instances.size() != current.prefab_instances.size()) {
    return GNEISS_ERROR_INVALID_STATE;
  }
  for (std::size_t index = 0; index < prefab_instances.size(); ++index) {
    auto& author = current.prefab_instances[index];
    gneiss_transform transform = GNEISS_TRANSFORM_IDENTITY;
    const auto result =
        gneiss_scene_node_get_local_transform(world_, prefab_instances[index]->root(), &transform);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    std::ranges::copy(transform.translation, author.translation.begin());
    std::ranges::copy(transform.rotation, author.rotation.begin());
    std::ranges::copy(transform.scale, author.scale.begin());
  }
  return serialize_scene_description(current, out_json);
}

scene_instance_service::scene_instance_service(
    gneiss_world world, const asset_internal::virtual_file_system& file_system,
    render_internal::render_asset_loader& loader, prefab_asset_loader& prefab_loader) noexcept
    : world_(world), file_system_(file_system), loader_(loader), prefab_loader_(prefab_loader),
      domain_(allocate_domain()), instances_(domain_) {}

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
    auto instance = std::make_unique<scene_instance>(world_, loader_, prefab_loader_);
    result = stage_assets(description, loader_, *instance);
    if (result == GNEISS_SUCCESS) {
      result = commit_scene(world_, description, *instance);
    }
    if (result == GNEISS_SUCCESS) {
      result = commit_prefab_instances(world_, description, prefab_loader_, loader_, *instance);
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

gneiss_result scene_instance_service::create_empty(std::string_view scene_uuid,
                                                   gneiss_scene_instance* out_instance) noexcept {
  if (out_instance == nullptr || !is_valid()) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_instance = GNEISS_NULL_SCENE_INSTANCE;
  try {
    const std::string json = std::string{"{\"format\":\"gneiss.scene\",\"version\":3,"} +
                             "\"scene_uuid\":\"" + std::string{scene_uuid} +
                             "\",\"objects\":[],\"prefab_instances\":[]}";
    scene_description description;
    scene_diagnostic diagnostic;
    const auto result = parse_scene_description(json, description, diagnostic);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    auto instance = std::make_unique<scene_instance>(world_, loader_, prefab_loader_);
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
scene_instance_service::get_prefab_node_count(gneiss_scene_instance instance,
                                              std::uint64_t* out_count) const noexcept {
  if (out_count == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_count = 0U;
  const auto* value = instances_.get(instance, core::resource_type::scene_instance);
  if (value == nullptr || *value == nullptr) {
    return GNEISS_ERROR_INVALID_HANDLE;
  }
  *out_count = (*value)->get_prefab_node_count();
  return GNEISS_SUCCESS;
}

gneiss_result scene_instance_service::get_prefab_node_info(
    gneiss_scene_instance instance, std::uint64_t index,
    gneiss_scene_prefab_node_info* out_info) const noexcept {
  if (out_info == nullptr || out_info->struct_size < sizeof(gneiss_scene_prefab_node_info)) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  const auto struct_size = out_info->struct_size;
  *out_info = GNEISS_SCENE_PREFAB_NODE_INFO_INIT;
  out_info->struct_size = struct_size;
  const auto* value = instances_.get(instance, core::resource_type::scene_instance);
  return value == nullptr || *value == nullptr ? GNEISS_ERROR_INVALID_HANDLE
                                               : (*value)->get_prefab_node_info(index, *out_info);
}

gneiss_result
scene_instance_service::create_prefab_instance(gneiss_scene_instance instance,
                                               const gneiss_scene_prefab_instance_desc& desc,
                                               gneiss_scene_node_id* out_root) noexcept {
  if (out_root == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_root = GNEISS_NULL_SCENE_NODE_ID;
  try {
    auto* value = instances_.get(instance, core::resource_type::scene_instance);
    return value == nullptr || *value == nullptr ? GNEISS_ERROR_INVALID_HANDLE
                                                 : (*value)->create_prefab_instance(desc, out_root);
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result scene_instance_service::set_prefab_instance_name(gneiss_scene_instance instance,
                                                               gneiss_scene_node_id root,
                                                               std::string_view name) noexcept {
  try {
    auto* value = instances_.get(instance, core::resource_type::scene_instance);
    return value == nullptr || *value == nullptr ? GNEISS_ERROR_INVALID_HANDLE
                                                 : (*value)->set_prefab_instance_name(root, name);
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result scene_instance_service::destroy_prefab_instance(gneiss_scene_instance instance,
                                                              gneiss_scene_node_id root) noexcept {
  auto* value = instances_.get(instance, core::resource_type::scene_instance);
  return value == nullptr || *value == nullptr ? GNEISS_ERROR_INVALID_HANDLE
                                               : (*value)->destroy_prefab_instance(root);
}

gneiss_result scene_instance_service::refresh_prefab_instance(
    gneiss_scene_instance instance, gneiss_scene_node_id root, gneiss_scene_node_id* out_new_root,
    gneiss_scene_prefab_refresh_token* out_token) noexcept {
  if (out_new_root == nullptr || out_token == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_new_root = GNEISS_NULL_SCENE_NODE_ID;
  *out_token = GNEISS_NULL_SCENE_PREFAB_REFRESH_TOKEN;
  try {
    auto* value = instances_.get(instance, core::resource_type::scene_instance);
    return value == nullptr || *value == nullptr
               ? GNEISS_ERROR_INVALID_HANDLE
               : (*value)->refresh_prefab_instance(root, out_new_root, out_token);
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result
scene_instance_service::toggle_prefab_refresh(gneiss_scene_instance instance,
                                              gneiss_scene_prefab_refresh_token token,
                                              gneiss_scene_node_id* out_new_root) noexcept {
  if (out_new_root == nullptr || token == GNEISS_NULL_SCENE_PREFAB_REFRESH_TOKEN) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_new_root = GNEISS_NULL_SCENE_NODE_ID;
  try {
    auto* value = instances_.get(instance, core::resource_type::scene_instance);
    return value == nullptr || *value == nullptr
               ? GNEISS_ERROR_INVALID_HANDLE
               : (*value)->toggle_prefab_refresh(token, out_new_root);
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result
scene_instance_service::release_prefab_refresh(gneiss_scene_instance instance,
                                               gneiss_scene_prefab_refresh_token token) noexcept {
  if (token == GNEISS_NULL_SCENE_PREFAB_REFRESH_TOKEN) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  auto* value = instances_.get(instance, core::resource_type::scene_instance);
  return value == nullptr || *value == nullptr ? GNEISS_ERROR_INVALID_HANDLE
                                               : (*value)->release_prefab_refresh(token);
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
  if (struct_size >= GNEISS_SCENE_INSTANCE_NODE_INFO_VERSION_3_SIZE) {
    out_info->local_transform = GNEISS_TRANSFORM_IDENTITY;
    out_info->component_flags = 0U;
    out_info->reserved_3 = 0U;
    out_info->camera = GNEISS_CAMERA_DESC_INIT;
  }
  out_info->struct_size = struct_size;
  const auto* value = instances_.get(instance, core::resource_type::scene_instance);
  if (value == nullptr || *value == nullptr) {
    return GNEISS_ERROR_INVALID_HANDLE;
  }
  return (*value)->get_node_info(index, *out_info);
}

gneiss_result scene_instance_service::create_node(gneiss_scene_instance instance,
                                                  const gneiss_scene_node_desc& desc,
                                                  gneiss_scene_node_id* out_node) noexcept {
  if (out_node == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_node = GNEISS_NULL_SCENE_NODE_ID;
  try {
    auto* value = instances_.get(instance, core::resource_type::scene_instance);
    return value == nullptr || *value == nullptr ? GNEISS_ERROR_INVALID_HANDLE
                                                 : (*value)->create_node(desc, out_node);
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result scene_instance_service::set_node_name(gneiss_scene_instance instance,
                                                    gneiss_scene_node_id node,
                                                    std::string_view name) noexcept {
  try {
    auto* value = instances_.get(instance, core::resource_type::scene_instance);
    return value == nullptr || *value == nullptr ? GNEISS_ERROR_INVALID_HANDLE
                                                 : (*value)->set_node_name(node, name);
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result scene_instance_service::reparent_node(gneiss_scene_instance instance,
                                                    gneiss_scene_node_id node,
                                                    gneiss_scene_node_id parent) noexcept {
  try {
    auto* value = instances_.get(instance, core::resource_type::scene_instance);
    return value == nullptr || *value == nullptr ? GNEISS_ERROR_INVALID_HANDLE
                                                 : (*value)->reparent_node(node, parent);
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result scene_instance_service::capture_subtree(gneiss_scene_instance instance,
                                                      gneiss_scene_node_id root,
                                                      std::string& out_snapshot) const noexcept {
  try {
    const auto* value = instances_.get(instance, core::resource_type::scene_instance);
    return value == nullptr || *value == nullptr ? GNEISS_ERROR_INVALID_HANDLE
                                                 : (*value)->capture_subtree(root, out_snapshot);
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result scene_instance_service::restore_subtree(gneiss_scene_instance instance,
                                                      std::string_view snapshot,
                                                      gneiss_scene_node_id parent,
                                                      const gneiss_scene_uuid_mapping* mappings,
                                                      std::uint64_t mapping_count,
                                                      gneiss_scene_node_id* out_root) noexcept {
  if (out_root == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_root = GNEISS_NULL_SCENE_NODE_ID;
  try {
    auto* value = instances_.get(instance, core::resource_type::scene_instance);
    return value == nullptr || *value == nullptr
               ? GNEISS_ERROR_INVALID_HANDLE
               : (*value)->restore_subtree(snapshot, parent, mappings, mapping_count, out_root);
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result scene_instance_service::destroy_subtree(gneiss_scene_instance instance,
                                                      gneiss_scene_node_id root) noexcept {
  try {
    auto* value = instances_.get(instance, core::resource_type::scene_instance);
    return value == nullptr || *value == nullptr ? GNEISS_ERROR_INVALID_HANDLE
                                                 : (*value)->destroy_subtree(root);
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
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

gneiss_result scene_instance_service::set_camera(gneiss_scene_instance instance,
                                                 gneiss_scene_node_id node,
                                                 const gneiss_scene_camera_desc& desc) noexcept {
  auto* value = instances_.get(instance, core::resource_type::scene_instance);
  return value == nullptr || *value == nullptr ? GNEISS_ERROR_INVALID_HANDLE
                                               : (*value)->set_camera(node, desc);
}

gneiss_result scene_instance_service::remove_camera(gneiss_scene_instance instance,
                                                    gneiss_scene_node_id node) noexcept {
  auto* value = instances_.get(instance, core::resource_type::scene_instance);
  return value == nullptr || *value == nullptr ? GNEISS_ERROR_INVALID_HANDLE
                                               : (*value)->remove_camera(node);
}

gneiss_result scene_instance_service::remove_mesh_renderer(gneiss_scene_instance instance,
                                                           gneiss_scene_node_id node) noexcept {
  auto* value = instances_.get(instance, core::resource_type::scene_instance);
  return value == nullptr || *value == nullptr ? GNEISS_ERROR_INVALID_HANDLE
                                               : (*value)->remove_mesh_renderer(node);
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
