// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/reflection/type_registry.hpp"

#include "engine/function/world/world_service.hpp"

#include "engine/function/scene/scene_instance_service.hpp"

#include "engine/asset/virtual_file_system.hpp"
#include "engine/function/scene/scene_description.hpp"
#include "engine/function/scene/structural_diff.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>
#include <map>
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
  auto result = gneiss::world_internal::entity_create(world, target.entity);
  if (result == GNEISS_SUCCESS) {
    result = gneiss::world_internal::node_create(world, parent, target.entity, target.node);
  }
  const auto transform = to_transform(source);
  if (result == GNEISS_SUCCESS) {
    result = gneiss::world_internal::node_set_local_transform(world, target.node, transform);
  }
  if (result == GNEISS_SUCCESS && source.camera) {
    const gneiss::world_internal::camera_settings camera{
        .vertical_field_of_view_radians = source.camera->vertical_field_of_view_radians,
        .near_plane = source.camera->near_plane,
        .far_plane = source.camera->far_plane};
    result = gneiss::world_internal::entity_configure_camera(world, target.entity, camera);
    if (result == GNEISS_SUCCESS && source.camera->is_primary) {
      result = gneiss::world_internal::set_active_camera(world, target.entity);
    }
  }
  if (result == GNEISS_SUCCESS && source.mesh_renderer) {
    const gneiss_mesh_renderer renderer{.mesh = target.mesh.get(),
                                        .material = target.material.get()};
    result = gneiss::world_internal::entity_set_mesh_renderer(world, target.entity, renderer);
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
                                      gneiss_type_registry registry, scene_instance& instance) {
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
    result = prefab_runtime_instance::create(world, render_loader, std::move(lease), registry,
                                             source.instance_uuid, parent, to_transform(source),
                                             source.overrides, runtime);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    instance.prefab_instances.push_back(std::move(runtime));
  }
  return GNEISS_SUCCESS;
}

[[nodiscard]] bool equal_prefab_instances(const prefab_instance_description& left,
                                          const prefab_instance_description& right) noexcept {
  if (left.instance_uuid != right.instance_uuid || left.name != right.name ||
      left.parent_uuid != right.parent_uuid || left.prefab_uri != right.prefab_uri ||
      left.translation != right.translation || left.rotation != right.rotation ||
      left.scale != right.scale || left.overrides.size() != right.overrides.size()) {
    return false;
  }
  for (std::size_t index = 0U; index < left.overrides.size(); ++index) {
    if (!(left.overrides[index].key == right.overrides[index].key) ||
        !(left.overrides[index].value == right.overrides[index].value)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool equal_prefab_container(const scene_description& left,
                                          const scene_description& right) noexcept {
  return left.prefab_instances.size() == right.prefab_instances.size() &&
         std::ranges::equal(left.prefab_instances, right.prefab_instances, equal_prefab_instances);
}

gneiss_result apply_camera(gneiss_world world, gneiss_entity_id entity,
                           const std::optional<camera_description>& camera) noexcept {
  if (!camera) {
    const auto result = gneiss::world_internal::entity_remove_camera(world, entity);
    return result == GNEISS_ERROR_NOT_FOUND ? GNEISS_SUCCESS : result;
  }
  const gneiss_camera value{.vertical_field_of_view_radians =
                                camera->vertical_field_of_view_radians,
                            .near_plane = camera->near_plane,
                            .far_plane = camera->far_plane,
                            .is_primary = static_cast<std::uint8_t>(camera->is_primary ? 1U : 0U),
                            .reserved = {0U, 0U, 0U}};
  return gneiss::world_internal::entity_set_camera(world, entity, value);
}

gneiss_result apply_mesh_renderer(gneiss_world world, scene_instance::object& object,
                                  bool present) noexcept {
  if (!present) {
    const auto result = gneiss::world_internal::entity_remove_mesh_renderer(world, object.entity);
    return result == GNEISS_ERROR_NOT_FOUND ? GNEISS_SUCCESS : result;
  }
  const gneiss_mesh_renderer renderer{.mesh = object.mesh.get(), .material = object.material.get()};
  return gneiss::world_internal::entity_set_mesh_renderer(world, object.entity, renderer);
}

} // namespace

scene_instance::scene_instance(gneiss_world world, render_internal::render_asset_loader& loader,
                               prefab_asset_loader& prefab_loader,
                               gneiss_type_registry registry) noexcept
    : world_(world), loader_(loader), prefab_loader_(prefab_loader), registry_(registry) {}

scene_instance::~scene_instance() noexcept { rollback(); }

void scene_instance::initialize_staged(scene_description candidate) {
  description = std::move(candidate);
  objects.resize(description.objects.size());
  prefab_instances.reserve(description.prefab_instances.size());
}

gneiss_result scene_instance::create_staged_node(std::size_t index, gneiss_scene_node_id parent,
                                                 render_internal::mesh_asset_lease mesh,
                                                 render_internal::material_asset_lease material) {
  if (index >= objects.size() || objects[index].entity != GNEISS_NULL_ENTITY_ID) {
    return GNEISS_ERROR_INVALID_STATE;
  }
  const auto& source = description.objects[index];
  auto& target = objects[index];
  target.uuid = source.uuid;
  target.name = source.name;
  if (source.mesh_renderer) {
    if (!mesh || !material) {
      return GNEISS_ERROR_NOT_READY;
    }
    target.mesh = std::move(mesh);
    target.material = std::move(material);
  }
  return commit_object(world_, source, target, parent);
}

void scene_instance::rollback() noexcept {
  prefab_instances.clear();
  for (auto iterator = objects.rbegin(); iterator != objects.rend(); ++iterator) {
    if (iterator->entity != GNEISS_NULL_ENTITY_ID) {
      (void)gneiss::world_internal::entity_destroy(world_, iterator->entity);
      iterator->entity = GNEISS_NULL_ENTITY_ID;
    }
    if (iterator->node != GNEISS_NULL_SCENE_NODE_ID) {
      (void)gneiss::world_internal::node_destroy(world_, iterator->node);
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

gneiss_result scene_instance::reload(scene_description candidate) {
  if (candidate.uuid != description.uuid || !equal_prefab_container(description, candidate)) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }

  structural_diff diff;
  auto result = build_structural_diff(description.objects, candidate.objects, diff);
  if (result != GNEISS_SUCCESS) {
    return result;
  }

  std::vector<object> staged;
  staged.reserve(candidate.objects.size());
  std::unordered_map<std::string_view, std::size_t> old_indices;
  old_indices.reserve(objects.size());
  for (std::size_t index = 0U; index < objects.size(); ++index) {
    old_indices.emplace(objects[index].uuid, index);
  }
  for (const auto& source : candidate.objects) {
    object target;
    target.uuid = source.uuid;
    target.name = source.name;
    if (const auto previous = old_indices.find(source.uuid); previous != old_indices.end()) {
      target.entity = objects[previous->second].entity;
      target.node = objects[previous->second].node;
    }
    if (source.mesh_renderer) {
      render_internal::asset_diagnostic diagnostic;
      result = loader_.acquire_mesh(source.mesh_renderer->mesh_uri, target.mesh, diagnostic);
      if (result == GNEISS_SUCCESS) {
        result = loader_.acquire_material(source.mesh_renderer->material_uri, target.material,
                                          diagnostic);
      }
      if (result != GNEISS_SUCCESS) {
        return result;
      }
    }
    staged.push_back(std::move(target));
  }

  std::unordered_map<std::string_view, gneiss_scene_node_id> nodes;
  nodes.reserve(staged.size());
  for (const auto& target : staged) {
    if (target.node != GNEISS_NULL_SCENE_NODE_ID) {
      nodes.emplace(target.uuid, target.node);
    }
  }

  std::vector<std::size_t> committed_additions;
  committed_additions.reserve(diff.added.size());
  gneiss_entity_id active_camera_before = GNEISS_NULL_ENTITY_ID;
  (void)gneiss::world_internal::get_active_camera(world_, active_camera_before);
  const auto rollback_additions = [&]() noexcept {
    for (auto iterator = committed_additions.rbegin(); iterator != committed_additions.rend();
         ++iterator) {
      auto& target = staged[*iterator];
      if (target.entity != GNEISS_NULL_ENTITY_ID) {
        (void)gneiss::world_internal::entity_destroy(world_, target.entity);
      }
      if (target.node != GNEISS_NULL_SCENE_NODE_ID) {
        (void)gneiss::world_internal::node_destroy(world_, target.node);
      }
      target.entity = GNEISS_NULL_ENTITY_ID;
      target.node = GNEISS_NULL_SCENE_NODE_ID;
    }
    if (active_camera_before != GNEISS_NULL_ENTITY_ID) {
      (void)gneiss::world_internal::set_active_camera(world_, active_camera_before);
    }
  };
  for (const auto& addition : diff.added) {
    const auto& source = candidate.objects[addition.new_index];
    const auto parent =
        source.parent_uuid ? nodes.at(*source.parent_uuid) : GNEISS_NULL_SCENE_NODE_ID;
    result = commit_object(world_, source, staged[addition.new_index], parent);
    if (result != GNEISS_SUCCESS) {
      auto& failed = staged[addition.new_index];
      if (failed.entity != GNEISS_NULL_ENTITY_ID) {
        (void)gneiss::world_internal::entity_destroy(world_, failed.entity);
      }
      if (failed.node != GNEISS_NULL_SCENE_NODE_ID) {
        (void)gneiss::world_internal::node_destroy(world_, failed.node);
      }
      rollback_additions();
      return result;
    }
    nodes.emplace(source.uuid, staged[addition.new_index].node);
    committed_additions.push_back(addition.new_index);
  }

  struct update_snapshot final {
    std::size_t old_index;
    std::size_t new_index;
    structural_change changes;
    gneiss_scene_node_id parent;
    gneiss_transform transform;
    std::optional<camera_description> camera;
  };
  std::vector<update_snapshot> applied;
  applied.reserve(diff.updated.size());
  const auto rollback_updates = [&]() noexcept {
    for (auto iterator = applied.rbegin(); iterator != applied.rend(); ++iterator) {
      const auto& snapshot = *iterator;
      auto& target = staged[snapshot.new_index];
      if (has_change(snapshot.changes, structural_change::parent)) {
        (void)gneiss::world_internal::node_reparent(world_, target.node, snapshot.parent);
      }
      if (has_change(snapshot.changes, structural_change::transform)) {
        (void)gneiss::world_internal::node_set_local_transform(world_, target.node,
                                                               snapshot.transform);
      }
      if (has_change(snapshot.changes, structural_change::camera)) {
        (void)apply_camera(world_, target.entity, snapshot.camera);
      }
      if (has_change(snapshot.changes, structural_change::mesh_renderer)) {
        auto& old_object = objects[snapshot.old_index];
        (void)apply_mesh_renderer(
            world_, old_object, description.objects[snapshot.old_index].mesh_renderer.has_value());
      }
    }
  };

  for (const auto& update : diff.updated) {
    auto& target = staged[update.new_index];
    update_snapshot snapshot{};
    snapshot.old_index = update.old_index;
    snapshot.new_index = update.new_index;
    snapshot.changes = update.changes;
    result = gneiss::world_internal::node_get_parent(world_, target.node, snapshot.parent);
    if (result == GNEISS_SUCCESS) {
      result =
          gneiss::world_internal::node_get_local_transform(world_, target.node, snapshot.transform);
    }
    if (result == GNEISS_SUCCESS && has_change(update.changes, structural_change::camera)) {
      gneiss::world_internal::camera_settings camera;
      const auto camera_result =
          gneiss::world_internal::entity_get_camera(world_, target.entity, camera);
      if (camera_result == GNEISS_SUCCESS) {
        gneiss_entity_id active = GNEISS_NULL_ENTITY_ID;
        const bool primary =
            gneiss::world_internal::get_active_camera(world_, active) == GNEISS_SUCCESS &&
            active == target.entity;
        snapshot.camera = camera_description{.vertical_field_of_view_radians =
                                                 camera.vertical_field_of_view_radians,
                                             .near_plane = camera.near_plane,
                                             .far_plane = camera.far_plane,
                                             .is_primary = primary};
      } else if (camera_result != GNEISS_ERROR_NOT_FOUND) {
        result = camera_result;
      }
    }
    if (result != GNEISS_SUCCESS) {
      rollback_updates();
      rollback_additions();
      return result;
    }
    applied.push_back(snapshot);

    const auto& source = candidate.objects[update.new_index];
    if (has_change(update.changes, structural_change::parent)) {
      const auto parent =
          source.parent_uuid ? nodes.at(*source.parent_uuid) : GNEISS_NULL_SCENE_NODE_ID;
      result = gneiss::world_internal::node_reparent(world_, target.node, parent);
    }
    if (result == GNEISS_SUCCESS && has_change(update.changes, structural_change::transform)) {
      const auto transform = to_transform(source);
      result = gneiss::world_internal::node_set_local_transform(world_, target.node, transform);
    }
    if (result == GNEISS_SUCCESS && has_change(update.changes, structural_change::camera)) {
      result = apply_camera(world_, target.entity, source.camera);
    }
    if (result == GNEISS_SUCCESS && has_change(update.changes, structural_change::mesh_renderer)) {
      result = apply_mesh_renderer(world_, target, source.mesh_renderer.has_value());
    }
    if (result != GNEISS_SUCCESS) {
      rollback_updates();
      rollback_additions();
      return result;
    }
  }

  for (const auto& removal : diff.removed) {
    auto& target = objects[removal.old_index];
    result = gneiss::world_internal::entity_destroy(world_, target.entity);
    if (result == GNEISS_SUCCESS) {
      result = gneiss::world_internal::node_destroy(world_, target.node);
    }
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    target.entity = GNEISS_NULL_ENTITY_ID;
    target.node = GNEISS_NULL_SCENE_NODE_ID;
  }

  objects = std::move(staged);
  description = std::move(candidate);
  loader_.release_unused();
  return GNEISS_SUCCESS;
}

gneiss_result scene_instance::reload_prefab(std::string_view uri) {
  std::vector<std::size_t> affected;
  affected.reserve(description.prefab_instances.size());
  for (std::size_t index = 0U; index < description.prefab_instances.size(); ++index) {
    if (description.prefab_instances[index].prefab_uri == uri) {
      affected.push_back(index);
    }
  }
  if (affected.empty()) {
    return GNEISS_ERROR_NOT_FOUND;
  }

  prefab_asset_lease candidate;
  scene_diagnostic diagnostic;
  auto result = prefab_loader_.reload(uri, candidate, diagnostic);
  if (result != GNEISS_SUCCESS) {
    return result;
  }
  for (const auto index : affected) {
    result = prefab_instances[index]->validate_reload(
        candidate, registry_, description.prefab_instances[index].overrides);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
  }
  for (const auto index : affected) {
    result = prefab_instances[index]->reload(candidate, registry_,
                                             description.prefab_instances[index].overrides);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
  }
  std::erase_if(prefab_refresh_transactions_, [&](const auto& transaction) {
    return std::ranges::any_of(affected, [&](std::size_t index) {
      return description.prefab_instances[index].instance_uuid == transaction.instance_uuid;
    });
  });
  return GNEISS_SUCCESS;
}

gneiss_result scene_instance::get_node_info(std::uint64_t index, scene_node_view& out_info,
                                            node_query_detail detail) const {
  if (index >= objects.size()) {
    return GNEISS_ERROR_NOT_FOUND;
  }
  const auto& object = objects[static_cast<std::size_t>(index)];
  gneiss_entity_id entity = GNEISS_NULL_ENTITY_ID;
  auto result = gneiss::world_internal::node_get_entity(world_, object.node, entity);
  if (result != GNEISS_SUCCESS || entity == GNEISS_NULL_ENTITY_ID) {
    return result == GNEISS_SUCCESS ? GNEISS_ERROR_INVALID_HANDLE : result;
  }
  gneiss_scene_node_id parent = GNEISS_NULL_SCENE_NODE_ID;
  result = gneiss::world_internal::node_get_parent(world_, object.node, parent);
  if (result != GNEISS_SUCCESS) {
    return result;
  }
  out_info.node = object.node;
  out_info.parent = parent;
  out_info.entity = entity;
  out_info.uuid = object.uuid;
  out_info.name = object.name;
  if (detail != node_query_detail::identity) {
    const auto& author = description.objects[static_cast<std::size_t>(index)];
    if (author.mesh_renderer) {
      out_info.mesh_uri = author.mesh_renderer->mesh_uri;
      out_info.material_uri = author.mesh_renderer->material_uri;
    }
  }
  if (detail == node_query_detail::components) {
    const auto& author = description.objects[static_cast<std::size_t>(index)];
    result = gneiss::world_internal::entity_get_local_transform(world_, entity,
                                                                out_info.local_transform);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    out_info.component_flags = author.mesh_renderer != std::nullopt
                                   ? GNEISS_SCENE_NODE_COMPONENT_MESH_RENDERER
                                   : UINT32_C(0);
    if (author.camera) {
      gneiss::world_internal::camera_settings camera;
      result = gneiss::world_internal::entity_get_camera(world_, entity, camera);
      if (result != GNEISS_SUCCESS) {
        return result;
      }
      out_info.camera = camera_description{
          .vertical_field_of_view_radians = camera.vertical_field_of_view_radians,
          .near_plane = camera.near_plane,
          .far_plane = camera.far_plane,
          .is_primary = author.camera->is_primary,
      };
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
                                                   prefab_node_view& out_info) const {
  std::uint64_t offset = 0;
  for (std::size_t instance_index = 0; instance_index < prefab_instances.size(); ++instance_index) {
    const auto& runtime = *prefab_instances[instance_index];
    const auto& author = description.prefab_instances[instance_index];
    if (index == offset) {
      gneiss_scene_node_id parent = GNEISS_NULL_SCENE_NODE_ID;
      auto result = gneiss::world_internal::node_get_parent(world_, runtime.root(), parent);
      if (result != GNEISS_SUCCESS) {
        return result;
      }
      gneiss_transform transform = GNEISS_TRANSFORM_IDENTITY;
      result = gneiss::world_internal::node_get_local_transform(world_, runtime.root(), transform);
      if (result != GNEISS_SUCCESS) {
        return result;
      }
      out_info.flags = GNEISS_SCENE_PREFAB_NODE_INSTANCE_ROOT;
      out_info.node = runtime.root();
      out_info.parent = parent;
      out_info.entity = runtime.root_entity();
      out_info.instance_uuid = author.instance_uuid;
      out_info.name = author.name;
      out_info.prefab_uri = author.prefab_uri;
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
      out_info.instance_uuid = source.address->instance_uuid;
      out_info.source_node_uuid = source.address->source_node_uuid;
      out_info.name = source.name;
      out_info.prefab_uri = author.prefab_uri;
      const auto* source_object = runtime.find_source_object(source.address->source_node_uuid);
      if (source_object == nullptr) {
        return GNEISS_ERROR_INVALID_STATE;
      }
      out_info.source_local_transform = to_transform(*source_object);
      const auto transform_type = gneiss::world_internal::transform_type_id();
      for (const auto& override_value : author.overrides) {
        if (override_value.key.node.source_node_uuid != source.address->source_node_uuid ||
            !std::ranges::equal(override_value.key.type_id.bytes, transform_type.bytes)) {
          continue;
        }
        switch (override_value.key.field_id) {
        case GNEISS_TRANSFORM_FIELD_TRANSLATION:
          out_info.flags |= GNEISS_SCENE_PREFAB_NODE_TRANSLATION_OVERRIDDEN;
          break;
        case GNEISS_TRANSFORM_FIELD_ROTATION:
          out_info.flags |= GNEISS_SCENE_PREFAB_NODE_ROTATION_OVERRIDDEN;
          break;
        case GNEISS_TRANSFORM_FIELD_SCALE:
          out_info.flags |= GNEISS_SCENE_PREFAB_NODE_SCALE_OVERRIDDEN;
          break;
        default:
          break;
        }
      }
      const auto transform_result = gneiss::world_internal::node_get_local_transform(
          world_, source.node, out_info.local_transform);
      return transform_result;
    }
    offset += runtime.node_count();
  }
  return GNEISS_ERROR_NOT_FOUND;
}

gneiss_result
scene_instance_service::capture_prefab_header(gneiss_scene_instance instance, std::string_view uuid,
                                              prefab_instance_description& author,
                                              prefab_asset_lease& source) const noexcept try {
  std::uint64_t count{};
  const auto thread = world_internal::entity_count(world_, count);
  if (thread != GNEISS_SUCCESS) {
    return thread;
  }
  const auto* slot = instances_.get(instance, core::resource_type::scene_instance);
  if (slot == nullptr) {
    return GNEISS_ERROR_INVALID_HANDLE;
  }
  const auto& scene = **slot;
  const auto found = std::ranges::find(scene.description.prefab_instances, uuid,
                                       &prefab_instance_description::instance_uuid);
  if (found == scene.description.prefab_instances.end()) {
    return GNEISS_ERROR_NOT_FOUND;
  }
  const auto index = static_cast<std::size_t>(found - scene.description.prefab_instances.begin());
  auto copy = *found;
  gneiss_transform transform{};
  const auto queried = world_internal::node_get_local_transform(
      world_, scene.prefab_instances[index]->root(), transform);
  if (queried != GNEISS_SUCCESS) {
    return queried;
  }
  std::ranges::copy(transform.translation, copy.translation.begin());
  std::ranges::copy(transform.rotation, copy.rotation.begin());
  std::ranges::copy(transform.scale, copy.scale.begin());
  source = scene.prefab_instances[index]->prefab_lease();
  author = std::move(copy);
  return GNEISS_SUCCESS;
} catch (const std::bad_alloc&) {
  return GNEISS_ERROR_OUT_OF_MEMORY;
} catch (...) {
  return GNEISS_ERROR_INTERNAL;
}

gneiss_result scene_instance_service::capture_prefab_binding(gneiss_scene_instance instance,
                                                             std::string_view uuid,
                                                             std::size_t index,
                                                             mesh_binding& output) const noexcept {
  std::uint64_t count{};
  const auto thread = world_internal::entity_count(world_, count);
  if (thread != GNEISS_SUCCESS) {
    return thread;
  }
  const auto* slot = instances_.get(instance, core::resource_type::scene_instance);
  if (slot == nullptr) {
    return GNEISS_ERROR_INVALID_HANDLE;
  }
  const auto found = std::ranges::find_if(
      (*slot)->prefab_instances, [&](const auto& value) { return value->instance_uuid_ == uuid; });
  if (found == (*slot)->prefab_instances.end()) {
    return GNEISS_ERROR_NOT_FOUND;
  }
  if (index >= (*found)->nodes_.size()) {
    return GNEISS_ERROR_NOT_FOUND;
  }
  const auto& node = (*found)->nodes_[index];
  output = {.mesh = render_internal::render_asset_loader::asset_lease(node.mesh),
            .material = render_internal::render_asset_loader::asset_lease(node.material)};
  return GNEISS_SUCCESS;
}

gneiss_result scene_instance_service::begin_prefab_candidate(
    gneiss_scene_instance instance, const prefab_instance_description& author,
    prefab_asset_lease prefab, prefab_description prepared,
    std::unique_ptr<prefab_runtime_instance>& output, bool replacing) noexcept try {
  output.reset();
  std::uint64_t count{};
  const auto thread = world_internal::entity_count(world_, count);
  if (thread != GNEISS_SUCCESS) {
    return thread;
  }
  auto* slot = instances_.get(instance, core::resource_type::scene_instance);
  if (!slot) {
    return GNEISS_ERROR_INVALID_HANDLE;
  }
  auto& scene = **slot;
  const auto previous = std::ranges::find_if(scene.prefab_instances, [&](const auto& value) {
    return value->instance_uuid_ == author.instance_uuid;
  });
  if (!is_canonical_uuid(author.instance_uuid) ||
      scene.find_node(author.instance_uuid) != GNEISS_NULL_SCENE_NODE_ID ||
      (replacing ? previous == scene.prefab_instances.end()
                 : previous != scene.prefab_instances.end())) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  const auto parent =
      author.parent_uuid ? scene.find_node(*author.parent_uuid) : GNEISS_NULL_SCENE_NODE_ID;
  if (author.parent_uuid && !parent) {
    return GNEISS_ERROR_INVALID_HANDLE;
  }
  prefab_description effective;
  const auto validation = prefab_runtime_instance::prepare_candidate_description(
      prepared, registry_, author.instance_uuid, author.overrides, effective);
  if (validation != GNEISS_SUCCESS) {
    return validation;
  }
  const auto previous_root = replacing ? (*previous)->root() : 0U;
  scene.description.prefab_instances.reserve(scene.description.prefab_instances.size() + 1U);
  scene.prefab_instances.reserve(scene.prefab_instances.size() + 1U);
  auto candidate = std::make_unique<prefab_runtime_instance>(world_, loader_, std::move(prefab),
                                                             author.instance_uuid);
  gneiss_transform transform = GNEISS_TRANSFORM_IDENTITY;
  std::ranges::copy(author.translation, transform.translation);
  std::ranges::copy(author.rotation, transform.rotation);
  std::ranges::copy(author.scale, transform.scale);
  const auto result = candidate->begin_hidden(parent, transform, std::move(effective));
  candidate->candidate_scene_ = instance;
  candidate->candidate_previous_root_ = previous_root;
  // 即使根创建失败也交还候选，由协调者分步清理已分配的记录。
  output = std::move(candidate);
  return result;
} catch (const std::bad_alloc&) {
  return GNEISS_ERROR_OUT_OF_MEMORY;
} catch (...) {
  return GNEISS_ERROR_INTERNAL;
}

gneiss_result scene_instance_service::commit_prefab_candidate(
    gneiss_scene_instance instance, prefab_instance_description& author,
    std::unique_ptr<prefab_runtime_instance>& runtime) noexcept {
  std::uint64_t count{};
  const auto thread = world_internal::entity_count(world_, count);
  if (thread != GNEISS_SUCCESS) {
    return thread;
  }
  auto* slot = instances_.get(instance, core::resource_type::scene_instance);
  if (!slot || !runtime || runtime->world_ != world_ || runtime->candidate_scene_ != instance ||
      runtime->instance_uuid_ != author.instance_uuid) {
    return GNEISS_ERROR_INVALID_HANDLE;
  }
  auto& scene = **slot;
  if (scene.find_node(author.instance_uuid) != GNEISS_NULL_SCENE_NODE_ID ||
      scene.description.prefab_instances.size() == scene.description.prefab_instances.capacity() ||
      scene.prefab_instances.size() == scene.prefab_instances.capacity() ||
      std::ranges::any_of(scene.description.prefab_instances, [&](const auto& existing) {
        return existing.instance_uuid == author.instance_uuid;
      })) {
    return GNEISS_ERROR_INVALID_STATE;
  }
  const auto result = runtime->publish_hidden();
  if (result != GNEISS_SUCCESS) {
    return result;
  }
  scene.description.prefab_instances.push_back(std::move(author));
  scene.prefab_instances.push_back(std::move(runtime));
  return GNEISS_SUCCESS;
}

gneiss_result scene_instance_service::commit_prefab_replacements(
    gneiss_scene_instance instance, std::span<const prefab_replacement> replacements) noexcept try {
  std::uint64_t count{};
  const auto thread = world_internal::entity_count(world_, count);
  if (thread != GNEISS_SUCCESS) {
    return thread;
  }
  auto* slot = instances_.get(instance, core::resource_type::scene_instance);
  if (slot == nullptr) {
    return GNEISS_ERROR_INVALID_HANDLE;
  }
  if (replacements.empty()) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  auto& scene = **slot;
  std::vector<std::size_t> indices;
  std::vector<core::publication> previous;
  std::vector<core::publication> candidates;
  indices.reserve(replacements.size());
  previous.reserve(replacements.size());
  candidates.reserve(replacements.size());
  for (const auto& entry : replacements) {
    if (entry.author == nullptr || entry.runtime == nullptr || !*entry.runtime) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    const auto& candidate = **entry.runtime;
    if (candidate.world_ != world_ || candidate.candidate_scene_ != instance ||
        candidate.instance_uuid_ != entry.author->instance_uuid || !candidate.prepared_ ||
        candidate.root_node_ == GNEISS_NULL_SCENE_NODE_ID || candidate.published_ ||
        candidate.cancelling_ ||
        std::ranges::any_of(candidate.nodes_, [](const auto& node) { return node.node == 0U; })) {
      return GNEISS_ERROR_INVALID_STATE;
    }
    const auto found = std::ranges::find_if(scene.prefab_instances, [&](const auto& value) {
      return value->instance_uuid_ == candidate.instance_uuid_;
    });
    if (found == scene.prefab_instances.end() ||
        (*found)->root() != candidate.candidate_previous_root_) {
      return GNEISS_ERROR_INVALID_STATE;
    }
    indices.push_back(static_cast<std::size_t>(found - scene.prefab_instances.begin()));
    previous.push_back((*found)->publication_);
    candidates.push_back(candidate.publication_);
  }
  const auto result = world_internal::replace_publications(world_, previous, candidates);
  if (result != GNEISS_SUCCESS) {
    return result;
  }
  // 可失败操作全部在上面结束；交换作者记录与独占所有权，不分配、不派发外部回调。
  for (std::size_t index = 0; index < replacements.size(); ++index) {
    const auto& entry = replacements[index];
    auto& old = scene.prefab_instances[indices[index]];
    old->published_ = false;
    old->cancelling_ = true;
    auto& candidate = **entry.runtime;
    candidate.published_ = true;
    for (std::size_t node = 0; node < candidate.nodes_.size(); ++node) {
      const auto& camera = candidate.candidate_description().objects[node].camera;
      if (camera && camera->is_primary) {
        (void)world_internal::set_active_camera(world_, candidate.nodes_[node].entity);
      }
    }
    std::swap(scene.description.prefab_instances[indices[index]], *entry.author);
    old.swap(*entry.runtime);
  }
  return GNEISS_SUCCESS;
} catch (const std::bad_alloc&) {
  return GNEISS_ERROR_OUT_OF_MEMORY;
} catch (...) {
  return GNEISS_ERROR_INTERNAL;
}

gneiss_result scene_instance_service::prepare_prefab_removal(gneiss_scene_instance instance,
                                                             std::string_view uuid,
                                                             bool& complete) noexcept try {
  complete = false;
  std::uint64_t count{};
  const auto thread = world_internal::entity_count(world_, count);
  if (thread != GNEISS_SUCCESS) {
    return thread;
  }
  auto* slot = instances_.get(instance, core::resource_type::scene_instance);
  if (!slot) {
    return GNEISS_ERROR_INVALID_HANDLE;
  }
  const auto found = std::ranges::find_if(
      (*slot)->prefab_instances, [&](const auto& value) { return value->instance_uuid_ == uuid; });
  return found == (*slot)->prefab_instances.end() ? GNEISS_ERROR_NOT_FOUND
                                                  : (*found)->prepare_retirement_step(complete);
} catch (const std::bad_alloc&) {
  return GNEISS_ERROR_OUT_OF_MEMORY;
} catch (...) {
  return GNEISS_ERROR_INTERNAL;
}

gneiss_result scene_instance_service::commit_prefab_removal(
    gneiss_scene_instance instance, std::string_view uuid,
    std::unique_ptr<prefab_runtime_instance>& retired) noexcept {
  if (retired) {
    return GNEISS_ERROR_INVALID_STATE;
  }
  std::uint64_t count{};
  const auto thread = world_internal::entity_count(world_, count);
  if (thread != GNEISS_SUCCESS) {
    return thread;
  }
  auto* slot = instances_.get(instance, core::resource_type::scene_instance);
  if (!slot) {
    return GNEISS_ERROR_INVALID_HANDLE;
  }
  auto& scene = **slot;
  const auto found = std::ranges::find_if(
      scene.prefab_instances, [&](const auto& value) { return value->instance_uuid_ == uuid; });
  if (found == scene.prefab_instances.end()) {
    return GNEISS_ERROR_NOT_FOUND;
  }
  const auto result = (*found)->retire_hidden();
  if (result != GNEISS_SUCCESS) {
    return result;
  }
  const auto index = static_cast<std::size_t>(found - scene.prefab_instances.begin());
  retired = std::move(*found);
  scene.prefab_instances.erase(found);
  scene.description.prefab_instances.erase(scene.description.prefab_instances.begin() +
                                           static_cast<std::ptrdiff_t>(index));
  return GNEISS_SUCCESS;
}

gneiss_result scene_instance::create_prefab_instance(const prefab_creation& desc,
                                                     gneiss_scene_node_id* out_root) {
  *out_root = GNEISS_NULL_SCENE_NODE_ID;
  const auto instance_uuid = desc.instance_uuid;
  const auto name = desc.name;
  const auto prefab_uri = desc.prefab_uri;
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
                                       .prefab_uri = std::string(prefab_uri),
                                       .overrides = {}};
    std::ranges::copy(desc.local_transform.translation, author.translation.begin());
    std::ranges::copy(desc.local_transform.rotation, author.rotation.begin());
    std::ranges::copy(desc.local_transform.scale, author.scale.begin());
    description.prefab_instances.reserve(description.prefab_instances.size() + 1U);
    prefab_instances.reserve(prefab_instances.size() + 1U);
    std::unique_ptr<prefab_runtime_instance> runtime;
    result = prefab_runtime_instance::create(world_, loader_, std::move(lease), registry_,
                                             instance_uuid, desc.parent, desc.local_transform,
                                             author.overrides, runtime);
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

gneiss_result scene_instance::set_prefab_source_transform(gneiss_scene_node_id node,
                                                          const gneiss_transform& transform) {
  for (std::size_t index = 0U; index < prefab_instances.size(); ++index) {
    auto& runtime = *prefab_instances[index];
    prefab_runtime_instance::node_info info;
    for (std::size_t node_index = 0U; node_index < runtime.node_count(); ++node_index) {
      if (runtime.get_node_info(node_index, info) != GNEISS_SUCCESS || info.node != node) {
        continue;
      }
      const auto* source = runtime.find_source_object(info.address->source_node_uuid);
      if (source == nullptr) {
        return GNEISS_ERROR_INVALID_STATE;
      }
      auto pending = description.prefab_instances[index].overrides;
      const auto type_id = gneiss::world_internal::transform_type_id();
      const std::array candidates{
          prefab_property_override{.key = {.node = *info.address,
                                           .type_id = type_id,
                                           .field_id = GNEISS_TRANSFORM_FIELD_TRANSLATION},
                                   .value = {.payload = std::to_array(transform.translation)}},
          prefab_property_override{.key = {.node = *info.address,
                                           .type_id = type_id,
                                           .field_id = GNEISS_TRANSFORM_FIELD_ROTATION},
                                   .value = {.payload = std::to_array(transform.rotation)}},
          prefab_property_override{.key = {.node = *info.address,
                                           .type_id = type_id,
                                           .field_id = GNEISS_TRANSFORM_FIELD_SCALE},
                                   .value = {.payload = std::to_array(transform.scale)}}};
      const std::array source_values{prefab_property_value{.payload = source->translation},
                                     prefab_property_value{.payload = source->rotation},
                                     prefab_property_value{.payload = source->scale}};
      for (std::size_t field_index = 0U; field_index < candidates.size(); ++field_index) {
        const auto result = set_prefab_property_override(
            registry_, pending, candidates[field_index], source_values[field_index]);
        if (result != GNEISS_SUCCESS) {
          return result;
        }
      }
      const auto result = gneiss::world_internal::node_set_local_transform(world_, node, transform);
      if (result != GNEISS_SUCCESS) {
        return result;
      }
      description.prefab_instances[index].overrides.swap(pending);
      return GNEISS_SUCCESS;
    }
  }
  return GNEISS_ERROR_INVALID_HANDLE;
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
  auto result = gneiss::world_internal::node_get_parent(world_, root, parent);
  gneiss_transform transform = GNEISS_TRANSFORM_IDENTITY;
  if (result == GNEISS_SUCCESS) {
    result = gneiss::world_internal::node_get_local_transform(world_, root, transform);
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
  result = prefab_runtime_instance::create(world_, loader_, std::move(lease), registry_,
                                           author.instance_uuid, parent, transform,
                                           author.overrides, replacement);
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
  auto result = gneiss::world_internal::node_get_parent(world_, current->root(), parent);
  gneiss_transform transform = GNEISS_TRANSFORM_IDENTITY;
  if (result == GNEISS_SUCCESS) {
    result = gneiss::world_internal::node_get_local_transform(world_, current->root(), transform);
  }
  if (result != GNEISS_SUCCESS) {
    return result;
  }
  auto current_lease = current->prefab_lease();
  std::unique_ptr<prefab_runtime_instance> replacement;
  result = prefab_runtime_instance::create(world_, loader_, transaction->alternate, registry_,
                                           author->instance_uuid, parent, transform,
                                           author->overrides, replacement);
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

gneiss_result scene_instance::create_node(const node_creation& desc,
                                          gneiss_scene_node_id* out_node) {
  const auto uuid = desc.uuid;
  const auto name = desc.name;
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
        (void)gneiss::world_internal::entity_destroy(world_, target.entity);
      }
      if (target.node != GNEISS_NULL_SCENE_NODE_ID) {
        (void)gneiss::world_internal::node_destroy(world_, target.node);
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
    const auto result = gneiss::world_internal::node_reparent(world_, node, parent);
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
    // 当前子树恢复只处理普通作者节点；不能生成会遗漏 Prefab 的撤销快照。
    if (std::ranges::any_of(current.prefab_instances, [&included](const auto& instance) {
          return instance.parent_uuid && included.contains(*instance.parent_uuid);
        })) {
      return GNEISS_ERROR_UNSUPPORTED;
    }
    // 场景其他位置的 Prefab 不属于本次快照，保留它们会产生快照外父引用。
    current.prefab_instances.clear();
    std::erase_if(current.objects, [&included](const auto& candidate) {
      return !included.contains(candidate.uuid);
    });
    // 子树快照必须能独立解析；外部父节点由 restore_subtree 的 parent 参数提供。
    // 保留该引用会让删除子节点后的 Undo 在解析阶段因父节点不在快照内而失败。
    for (auto& candidate : current.objects) {
      if (candidate.uuid == found->uuid) {
        candidate.parent_uuid.reset();
        break;
      }
    }
    current.author_json =
        std::string{"{\"format\":\"gneiss.scene\",\"version\":4,\"scene_uuid\":\""} + current.uuid +
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
                                              std::span<const uuid_mapping> mappings,
                                              gneiss_scene_node_id* out_root) {
  scene_description subtree;
  scene_diagnostic diagnostic;
  auto result = parse_scene_description(snapshot, subtree, diagnostic);
  if (result != GNEISS_SUCCESS || subtree.objects.empty()) {
    return result == GNEISS_SUCCESS ? GNEISS_ERROR_INVALID_ARGUMENT : result;
  }
  if (!subtree.prefab_instances.empty()) {
    return GNEISS_ERROR_UNSUPPORTED;
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
    if (!mappings.empty()) {
      if (mappings.size() != subtree.objects.size()) {
        return GNEISS_ERROR_INVALID_ARGUMENT;
      }
      uuid_map.reserve(mappings.size());
      std::unordered_set<std::string> targets;
      targets.reserve(mappings.size());
      for (const auto& mapping : mappings) {
        const std::string source(mapping.source_uuid);
        const std::string target(mapping.target_uuid);
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
              (void)gneiss::world_internal::entity_destroy(world_, value.entity);
            }
            if (value.node != GNEISS_NULL_SCENE_NODE_ID) {
              (void)gneiss::world_internal::node_destroy(world_, value.node);
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
    if (std::ranges::any_of(description.prefab_instances, [&removed](const auto& instance) {
          return instance.parent_uuid && removed.contains(*instance.parent_uuid);
        })) {
      return GNEISS_ERROR_UNSUPPORTED;
    }
    std::vector<std::pair<object*, std::size_t>> ordered;
    ordered.reserve(removed.size());
    for (auto& value : objects) {
      if (!removed.contains(value.uuid)) {
        continue;
      }
      if (gneiss::world_internal::node_get_entity(world_, value.node, value.entity) !=
          GNEISS_SUCCESS) {
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
      auto destroy_result = gneiss::world_internal::entity_destroy(world_, value->entity);
      if (destroy_result != GNEISS_SUCCESS) {
        return destroy_result;
      }
      destroy_result = gneiss::world_internal::node_destroy(world_, value->node);
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

gneiss_result scene_instance::create_mesh_renderer_node(const mesh_renderer_node_creation& desc,
                                                        gneiss_scene_node_id* out_node,
                                                        const scene_mesh_binding* prepared) {
  using asset_loader = render_internal::render_asset_loader;
  if (prepared &&
      (prepared->mesh.type() != render_internal::render_asset_type::mesh ||
       prepared->material.type() != render_internal::render_asset_type::material ||
       !loader_.owns(prepared->mesh) || !loader_.owns(prepared->material) ||
       prepared->mesh.uri() != desc.mesh_uri || prepared->material.uri() != desc.material_uri)) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  const auto uuid = desc.uuid;
  const auto name = desc.name;
  const auto mesh_uri = desc.mesh_uri;
  const auto material_uri = desc.material_uri;
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
    auto result = GNEISS_SUCCESS;
    if (prepared) {
      target.mesh = asset_loader::mesh_lease(prepared->mesh);
      target.material = asset_loader::material_lease(prepared->material);
    } else {
      result = loader_.acquire_mesh(mesh_uri, target.mesh, diagnostic);
      if (result == GNEISS_SUCCESS) {
        result = loader_.acquire_material(material_uri, target.material, diagnostic);
      }
    }
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    result = commit_object(world_, author, target, desc.parent);
    if (result != GNEISS_SUCCESS) {
      if (target.entity != GNEISS_NULL_ENTITY_ID) {
        (void)gneiss::world_internal::entity_destroy(world_, target.entity);
      }
      if (target.node != GNEISS_NULL_SCENE_NODE_ID) {
        (void)gneiss::world_internal::node_destroy(world_, target.node);
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
    // 编辑资产选择不等于重载；未改变的 URI 继续使用节点选定的版本。
    using asset_loader = render_internal::render_asset_loader;
    auto mesh = asset_loader::asset_lease(found->mesh).uri() == mesh_uri
                    ? found->mesh
                    : render_internal::mesh_asset_lease{};
    auto material = asset_loader::asset_lease(found->material).uri() == material_uri
                        ? found->material
                        : render_internal::material_asset_lease{};
    render_internal::asset_diagnostic diagnostic;
    auto result = GNEISS_SUCCESS;
    if (!mesh) {
      result = loader_.acquire_mesh(mesh_uri, mesh, diagnostic);
    }
    if (result == GNEISS_SUCCESS && !material) {
      result = loader_.acquire_material(material_uri, material, diagnostic);
    }
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    const gneiss_mesh_renderer renderer{.mesh = mesh.get(), .material = material.get()};
    result = gneiss::world_internal::entity_set_mesh_renderer(world_, found->entity, renderer);
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
                                         const camera_description& desc) {
  const auto found = std::ranges::find(objects, node, &object::node);
  if (found == objects.end()) {
    return GNEISS_ERROR_INVALID_HANDLE;
  }
  const auto result = gneiss::world_internal::entity_configure_camera(
      world_, found->entity,
      {.vertical_field_of_view_radians = desc.vertical_field_of_view_radians,
       .near_plane = desc.near_plane,
       .far_plane = desc.far_plane});
  if (result != GNEISS_SUCCESS) {
    return result;
  }
  if (desc.is_primary) {
    const auto active_result = gneiss::world_internal::set_active_camera(world_, found->entity);
    if (active_result != GNEISS_SUCCESS) {
      return active_result;
    }
  }
  const auto index = static_cast<std::size_t>(std::distance(objects.begin(), found));
  if (desc.is_primary) {
    for (auto& author : description.objects) {
      if (author.camera) {
        author.camera->is_primary = false;
      }
    }
  }
  description.objects[index].camera = desc;
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
  const auto result = gneiss::world_internal::entity_remove_camera(world_, found->entity);
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
  const auto result = gneiss::world_internal::entity_remove_mesh_renderer(world_, found->entity);
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
  if (std::ranges::any_of(description.objects,
                          [&uuid](const auto& candidate) {
                            return candidate.parent_uuid && *candidate.parent_uuid == uuid;
                          }) ||
      std::ranges::any_of(description.prefab_instances, [&uuid](const auto& instance) {
        return instance.parent_uuid && *instance.parent_uuid == uuid;
      })) {
    return GNEISS_ERROR_INVALID_STATE;
  }
  auto result = gneiss::world_internal::entity_destroy(world_, found->entity);
  if (result != GNEISS_SUCCESS) {
    return result;
  }
  result = gneiss::world_internal::node_destroy(world_, found->node);
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
  const auto active_result = gneiss::world_internal::get_active_camera(world_, active_camera);
  if (active_result != GNEISS_SUCCESS && active_result != GNEISS_ERROR_NOT_READY) {
    return active_result;
  }
  for (std::size_t index = 0; index < objects.size(); ++index) {
    const auto& runtime = objects[index];
    auto& author = current.objects[index];
    gneiss_transform transform = GNEISS_TRANSFORM_IDENTITY;
    auto result =
        gneiss::world_internal::entity_get_local_transform(world_, runtime.entity, transform);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    std::ranges::copy(transform.translation, author.translation.begin());
    std::ranges::copy(transform.rotation, author.rotation.begin());
    std::ranges::copy(transform.scale, author.scale.begin());
    if (author.camera) {
      gneiss::world_internal::camera_settings camera;
      result = gneiss::world_internal::entity_get_camera(world_, runtime.entity, camera);
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
    const auto result = gneiss::world_internal::node_get_local_transform(
        world_, prefab_instances[index]->root(), transform);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    std::ranges::copy(transform.translation, author.translation.begin());
    std::ranges::copy(transform.rotation, author.rotation.begin());
    std::ranges::copy(transform.scale, author.scale.begin());
  }
  return serialize_scene_description(current, out_json);
}

gneiss_result scene_instance_service::capture_render_assets(
    std::vector<render_internal::render_asset_lease>& output) noexcept {
  try {
    const auto owner = world_internal::replace_mesh_renderers(world_, {});
    if (owner != GNEISS_SUCCESS) {
      return owner;
    }
    std::vector<render_internal::render_asset_lease> captured;
    std::unordered_set<std::uint64_t> seen;
    const auto append = [&](const auto& lease) {
      if (lease && seen.insert(lease.get()).second) {
        captured.push_back(render_internal::render_asset_loader::asset_lease(lease));
      }
    };
    instances_.for_each(core::resource_type::scene_instance, [&](const auto& instance) {
      for (const auto& node : instance->objects) {
        append(node.mesh);
        append(node.material);
      }
      for (const auto& prefab : instance->prefab_instances) {
        for (const auto& node : prefab->nodes_) {
          append(node.mesh);
          append(node.material);
        }
      }
    });
    output = std::move(captured);
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result
scene_instance_service::replace_render_assets(std::span<const asset_replacement> changes) noexcept {
  try {
    const auto owner = world_internal::replace_mesh_renderers(world_, {});
    if (owner != GNEISS_SUCCESS) {
      return owner;
    }
    std::map<std::uint64_t, const asset_replacement*> replacements;
    for (const auto& change : changes) {
      const auto type = change.previous.type();
      if ((type != render_internal::render_asset_type::mesh &&
           type != render_internal::render_asset_type::material) ||
          change.replacement.type() != type || change.previous.uri() != change.replacement.uri() ||
          !loader_.owns(change.previous) || !loader_.owns(change.replacement) ||
          !replacements.emplace(change.previous.get(), &change).second) {
        return GNEISS_ERROR_INVALID_ARGUMENT;
      }
    }
    struct binding_update {
      render_internal::mesh_asset_lease* mesh;
      render_internal::material_asset_lease* material;
      render_internal::mesh_asset_lease next_mesh;
      render_internal::material_asset_lease next_material;
    };
    std::vector<binding_update> bindings;
    std::vector<world_internal::mesh_renderer_replacement> renderers;
    const auto collect = [&](auto& node) {
      const auto mesh = replacements.find(node.mesh.get());
      const auto material = replacements.find(node.material.get());
      if (mesh == replacements.end() && material == replacements.end()) {
        return;
      }
      auto next_mesh =
          mesh == replacements.end()
              ? node.mesh
              : render_internal::render_asset_loader::mesh_lease(mesh->second->replacement);
      auto next_material =
          material == replacements.end()
              ? node.material
              : render_internal::render_asset_loader::material_lease(material->second->replacement);
      renderers.push_back({
          .entity = node.entity,
          .expected = {.mesh = node.mesh.get(), .material = node.material.get()},
          .replacement = {.mesh = next_mesh.get(), .material = next_material.get()},
      });
      bindings.push_back({
          .mesh = &node.mesh,
          .material = &node.material,
          .next_mesh = std::move(next_mesh),
          .next_material = std::move(next_material),
      });
    };
    instances_.for_each(core::resource_type::scene_instance, [&](auto& instance) {
      for (auto& node : instance->objects) {
        collect(node);
      }
      for (auto& prefab : instance->prefab_instances) {
        for (auto& node : prefab->nodes_) {
          collect(node);
        }
      }
    });
    const auto result = world_internal::replace_mesh_renderers(world_, renderers);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    // ECS 已统一切换；租约移动不分配且不抛异常，旧帧的数据引用仍由渲染服务持有。
    for (auto& binding : bindings) {
      *binding.mesh = std::move(binding.next_mesh);
      *binding.material = std::move(binding.next_material);
    }
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

scene_instance_service::scene_instance_service(
    gneiss_world world, const asset_internal::virtual_file_system& file_system,
    render_internal::render_asset_loader& loader, prefab_asset_loader& prefab_loader) noexcept
    : world_(world), file_system_(file_system), loader_(loader), prefab_loader_(prefab_loader),
      domain_(allocate_domain()), instances_(domain_) {
  gneiss_result result = GNEISS_ERROR_INTERNAL;
  try {
    result = gneiss::reflection_internal::create(registry_);
    if (result == GNEISS_SUCCESS) {
      result = gneiss::world_internal::register_reflection(registry_);
    }
    if (result == GNEISS_SUCCESS) {
      result = gneiss::reflection_internal::freeze(gneiss::reflection_internal::resolve(registry_));
    }
  } catch (...) {
    result = GNEISS_ERROR_INTERNAL;
  }
  if (result != GNEISS_SUCCESS && registry_ != GNEISS_NULL_TYPE_REGISTRY) {
    (void)gneiss::reflection_internal::destroy(registry_);
    registry_ = GNEISS_NULL_TYPE_REGISTRY;
  }
}

scene_instance_service::~scene_instance_service() noexcept {
  if (registry_ != GNEISS_NULL_TYPE_REGISTRY) {
    (void)gneiss::reflection_internal::destroy(registry_);
  }
}

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
    auto instance = std::make_unique<scene_instance>(world_, loader_, prefab_loader_, registry_);
    result = stage_assets(description, loader_, *instance);
    if (result == GNEISS_SUCCESS) {
      result = commit_scene(world_, description, *instance);
    }
    if (result == GNEISS_SUCCESS) {
      result = commit_prefab_instances(world_, description, prefab_loader_, loader_, registry_,
                                       *instance);
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

gneiss_result scene_instance_service::reload(gneiss_scene_instance instance,
                                             std::string_view uri) noexcept {
  try {
    auto* value = instances_.get(instance, core::resource_type::scene_instance);
    if (value == nullptr || *value == nullptr) {
      return GNEISS_ERROR_INVALID_HANDLE;
    }
    scene_description candidate;
    scene_diagnostic diagnostic;
    const auto result = load_scene_description(file_system_, uri, candidate, diagnostic);
    return result == GNEISS_SUCCESS ? (*value)->reload(std::move(candidate)) : result;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result scene_instance_service::reload_prefab(gneiss_scene_instance instance,
                                                    std::string_view uri) noexcept {
  try {
    auto* value = instances_.get(instance, core::resource_type::scene_instance);
    if (value == nullptr || *value == nullptr) {
      return GNEISS_ERROR_INVALID_HANDLE;
    }
    return (*value)->reload_prefab(uri);
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
    const std::string json = std::string{"{\"format\":\"gneiss.scene\",\"version\":4,"} +
                             "\"scene_uuid\":\"" + std::string{scene_uuid} +
                             "\",\"objects\":[],\"prefab_instances\":[]}";
    scene_description description;
    scene_diagnostic diagnostic;
    const auto result = parse_scene_description(json, description, diagnostic);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    auto instance = std::make_unique<scene_instance>(world_, loader_, prefab_loader_, registry_);
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

gneiss_result
scene_instance_service::get_prefab_node_info(gneiss_scene_instance instance, std::uint64_t index,
                                             prefab_node_view& out_info) const noexcept {
  out_info = {};
  const auto* value = instances_.get(instance, core::resource_type::scene_instance);
  return value == nullptr || *value == nullptr ? GNEISS_ERROR_INVALID_HANDLE
                                               : (*value)->get_prefab_node_info(index, out_info);
}

gneiss_result
scene_instance_service::create_prefab_instance(gneiss_scene_instance instance,
                                               const prefab_creation& desc,
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

gneiss_result
scene_instance_service::set_prefab_source_transform(gneiss_scene_instance instance,
                                                    gneiss_scene_node_id node,
                                                    const gneiss_transform& transform) noexcept {
  try {
    auto* value = instances_.get(instance, core::resource_type::scene_instance);
    return value == nullptr || *value == nullptr
               ? GNEISS_ERROR_INVALID_HANDLE
               : (*value)->set_prefab_source_transform(node, transform);
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
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

gneiss_result scene_instance_service::get_node_info(gneiss_scene_instance instance,
                                                    std::uint64_t index, scene_node_view& out_info,
                                                    node_query_detail detail) const noexcept {
  out_info = {};
  const auto* value = instances_.get(instance, core::resource_type::scene_instance);
  if (value == nullptr || *value == nullptr) {
    return GNEISS_ERROR_INVALID_HANDLE;
  }
  return (*value)->get_node_info(index, out_info, detail);
}

gneiss_result scene_instance_service::create_node(gneiss_scene_instance instance,
                                                  const node_creation& desc,
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
                                                      std::span<const uuid_mapping> mappings,
                                                      gneiss_scene_node_id* out_root) noexcept {
  if (out_root == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_root = GNEISS_NULL_SCENE_NODE_ID;
  try {
    auto* value = instances_.get(instance, core::resource_type::scene_instance);
    return value == nullptr || *value == nullptr
               ? GNEISS_ERROR_INVALID_HANDLE
               : (*value)->restore_subtree(snapshot, parent, mappings, out_root);
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
                                                  const mesh_renderer_node_creation& desc,
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

gneiss_result scene_instance_service::create_mesh_renderer_node_ready(
    gneiss_scene_instance instance, const mesh_renderer_node_creation& desc,
    const mesh_binding& prepared, gneiss_scene_node_id& output) noexcept try {
  output = GNEISS_NULL_SCENE_NODE_ID;
  const auto owner = world_internal::replace_mesh_renderers(world_, {});
  if (owner != GNEISS_SUCCESS) {
    return owner;
  }
  auto* value = instances_.get(instance, core::resource_type::scene_instance);
  return value == nullptr || *value == nullptr
             ? GNEISS_ERROR_INVALID_HANDLE
             : (*value)->create_mesh_renderer_node(desc, &output, &prepared);
} catch (const std::bad_alloc&) {
  return GNEISS_ERROR_OUT_OF_MEMORY;
} catch (...) {
  return GNEISS_ERROR_INTERNAL;
}

gneiss_result scene_instance_service::capture_mesh_binding(gneiss_scene_instance instance,
                                                           gneiss_scene_node_id node,
                                                           mesh_binding& output) const noexcept {
  output = {};
  const auto owner = world_internal::replace_mesh_renderers(world_, {});
  if (owner != GNEISS_SUCCESS) {
    return owner;
  }
  const auto* value = instances_.get(instance, core::resource_type::scene_instance);
  if (value == nullptr || *value == nullptr) {
    return GNEISS_ERROR_INVALID_HANDLE;
  }
  const auto found = std::ranges::find((*value)->objects, node, &scene_instance::object::node);
  if (found == (*value)->objects.end()) {
    return GNEISS_ERROR_INVALID_HANDLE;
  }
  output = {.mesh = render_internal::render_asset_loader::asset_lease(found->mesh),
            .material = render_internal::render_asset_loader::asset_lease(found->material)};
  return GNEISS_SUCCESS;
}

gneiss_result scene_instance_service::replace_mesh_binding(gneiss_scene_instance instance,
                                                           gneiss_scene_node_id node,
                                                           const mesh_binding& expected,
                                                           const mesh_binding& replacement) noexcept
    try {
  const auto owner = world_internal::replace_mesh_renderers(world_, {});
  if (owner != GNEISS_SUCCESS) {
    return owner;
  }
  using loader = render_internal::render_asset_loader;
  using type = render_internal::render_asset_type;
  if (replacement.mesh.type() != type::mesh || replacement.material.type() != type::material ||
      !loader_.owns(replacement.mesh) || !loader_.owns(replacement.material)) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  mesh_binding current;
  const auto captured = capture_mesh_binding(instance, node, current);
  if (captured != GNEISS_SUCCESS) {
    return captured;
  }
  if (current.mesh.get() != expected.mesh.get() ||
      current.material.get() != expected.material.get()) {
    return GNEISS_ERROR_INVALID_STATE;
  }
  auto& value = **instances_.get(instance, core::resource_type::scene_instance);
  const auto found = std::ranges::find(value.objects, node, &scene_instance::object::node);
  mesh_renderer_description author{.mesh_uri = std::string(replacement.mesh.uri()),
                                   .material_uri = std::string(replacement.material.uri())};
  const std::array changes{world_internal::mesh_renderer_replacement{
      .entity = found->entity,
      .expected = {expected.mesh.get(), expected.material.get()},
      .replacement = {replacement.mesh.get(), replacement.material.get()},
  }};
  const auto changed = world_internal::replace_mesh_renderers(world_, changes);
  if (changed != GNEISS_SUCCESS) {
    return changed;
  }
  const auto index = static_cast<std::size_t>(std::distance(value.objects.begin(), found));
  value.description.objects[index].mesh_renderer = std::move(author);
  found->mesh = loader::mesh_lease(replacement.mesh);
  found->material = loader::material_lease(replacement.material);
  return GNEISS_SUCCESS;
} catch (const std::bad_alloc&) {
  return GNEISS_ERROR_OUT_OF_MEMORY;
} catch (...) {
  return GNEISS_ERROR_INTERNAL;
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

gneiss_result
scene_instance_service::validate_editable_node(gneiss_scene_instance instance,
                                               gneiss_scene_node_id node) const noexcept {
  const auto* value = instances_.get(instance, core::resource_type::scene_instance);
  if (value == nullptr || *value == nullptr) {
    return GNEISS_ERROR_INVALID_HANDLE;
  }
  return std::ranges::find((*value)->objects, node, &scene_instance::object::node) ==
                 (*value)->objects.end()
             ? GNEISS_ERROR_INVALID_HANDLE
             : GNEISS_SUCCESS;
}

gneiss_result scene_instance_service::set_camera(gneiss_scene_instance instance,
                                                 gneiss_scene_node_id node,
                                                 const camera_description& desc) noexcept {
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
