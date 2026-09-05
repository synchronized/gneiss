// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "scene/prefab_runtime_instance.h"

#include "scene/structural_diff.h"

#include <algorithm>
#include <new>
#include <type_traits>
#include <unordered_map>

namespace {

gneiss_transform to_transform(const gneiss::scene_internal::object_description& object) noexcept {
  gneiss_transform value{};
  std::ranges::copy(object.translation, value.translation);
  std::ranges::copy(object.rotation, value.rotation);
  std::ranges::copy(object.scale, value.scale);
  return value;
}

[[nodiscard]] gneiss_result
to_native_value(const gneiss::scene_internal::prefab_property_value& source,
                gneiss_property_value& output) noexcept {
  output = GNEISS_PROPERTY_VALUE_INIT;
  return std::visit(
      [&](const auto& payload) noexcept -> gneiss_result {
        using value_type = std::decay_t<decltype(payload)>;
        if constexpr (std::is_same_v<value_type, std::monostate>) {
          return GNEISS_ERROR_INVALID_ARGUMENT;
        } else {
          if constexpr (std::is_same_v<value_type, bool>) {
            output.kind = GNEISS_PROPERTY_KIND_BOOL;
            output.payload.bool_value = payload ? UINT8_C(1) : UINT8_C(0);
          } else if constexpr (std::is_same_v<value_type, std::int64_t>) {
            output.kind = GNEISS_PROPERTY_KIND_INT64;
            output.payload.int64_value = payload;
          } else if constexpr (std::is_same_v<value_type, std::uint64_t>) {
            output.kind = GNEISS_PROPERTY_KIND_UINT64;
            output.payload.uint64_value = payload;
          } else if constexpr (std::is_same_v<value_type, float>) {
            output.kind = GNEISS_PROPERTY_KIND_FLOAT32;
            output.payload.float32_value = payload;
          } else if constexpr (std::is_same_v<value_type, double>) {
            output.kind = GNEISS_PROPERTY_KIND_FLOAT64;
            output.payload.float64_value = payload;
          } else if constexpr (std::is_same_v<value_type, std::string>) {
            output.kind = GNEISS_PROPERTY_KIND_STRING;
            output.payload.string_value = {payload.data(),
                                           static_cast<std::uint32_t>(payload.size())};
          } else if constexpr (std::is_same_v<value_type, std::array<std::uint8_t, 16>>) {
            output.kind = GNEISS_PROPERTY_KIND_TYPE_ID;
            std::ranges::copy(payload, output.payload.type_id_value.bytes);
          } else if constexpr (std::is_same_v<value_type, std::array<float, 3>>) {
            output.kind = GNEISS_PROPERTY_KIND_VEC3;
            output.payload.vec3_value = {payload[0], payload[1], payload[2]};
          } else {
            output.kind = GNEISS_PROPERTY_KIND_QUATERNION;
            output.payload.quaternion_value = {payload[0], payload[1], payload[2], payload[3]};
          }
          return GNEISS_SUCCESS;
        }
      },
      source.payload);
}

gneiss_result
apply_camera(gneiss_world world, gneiss_entity_id entity,
             const std::optional<gneiss::scene_internal::camera_description>& camera) noexcept {
  if (!camera) {
    const auto result = gneiss_world_entity_remove_camera(world, entity);
    return result == GNEISS_ERROR_NOT_FOUND ? GNEISS_SUCCESS : result;
  }
  const gneiss_camera value{.vertical_field_of_view_radians =
                                camera->vertical_field_of_view_radians,
                            .near_plane = camera->near_plane,
                            .far_plane = camera->far_plane,
                            .is_primary = static_cast<std::uint8_t>(camera->is_primary ? 1U : 0U),
                            .reserved = {0U, 0U, 0U}};
  return gneiss_world_entity_set_camera(world, entity, &value);
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
prefab_runtime_instance::validate_reload(const prefab_asset_lease& candidate,
                                         gneiss_type_registry registry,
                                         const std::vector<prefab_property_override>& overrides) {
  if (!candidate || !prefab_ || candidate.get()->uuid != prefab_.get()->uuid ||
      candidate.get()->objects.empty()) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  structural_diff diff;
  auto result = build_structural_diff(prefab_.get()->objects, candidate.get()->objects, diff);
  if (result != GNEISS_SUCCESS) {
    return result;
  }

  std::vector<render_internal::mesh_asset_lease> meshes(candidate.get()->objects.size());
  std::vector<render_internal::material_asset_lease> materials(candidate.get()->objects.size());
  for (std::size_t index = 0U; index < candidate.get()->objects.size(); ++index) {
    const auto& source = candidate.get()->objects[index];
    if (!source.mesh_renderer) {
      continue;
    }
    render_internal::asset_diagnostic diagnostic;
    result = loader_.acquire_mesh(source.mesh_renderer->mesh_uri, meshes[index], diagnostic);
    if (result == GNEISS_SUCCESS) {
      result = loader_.acquire_material(source.mesh_renderer->material_uri, materials[index],
                                        diagnostic);
    }
    if (result != GNEISS_SUCCESS) {
      return result;
    }
  }

  const auto camera_type = gneiss_camera_type_id();
  for (const auto& override_value : overrides) {
    if (override_value.key.node.instance_uuid != instance_uuid_) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    result = validate_prefab_property_override(registry, override_value);
    const auto source =
        std::ranges::find(candidate.get()->objects, override_value.key.node.source_node_uuid,
                          &object_description::uuid);
    if (result != GNEISS_SUCCESS || source == candidate.get()->objects.end()) {
      return result == GNEISS_SUCCESS ? GNEISS_ERROR_NOT_FOUND : result;
    }
    if (std::ranges::equal(override_value.key.type_id.bytes, camera_type.bytes) &&
        !source->camera) {
      return GNEISS_ERROR_INVALID_STATE;
    }
  }
  return GNEISS_SUCCESS;
}

gneiss_result
prefab_runtime_instance::reload(prefab_asset_lease candidate, gneiss_type_registry registry,
                                const std::vector<prefab_property_override>& overrides) {
  auto result = validate_reload(candidate, registry, overrides);
  if (result != GNEISS_SUCCESS) {
    return result;
  }
  structural_diff diff;
  result = build_structural_diff(prefab_.get()->objects, candidate.get()->objects, diff);
  if (result != GNEISS_SUCCESS) {
    return result;
  }

  std::vector<runtime_node> staged;
  staged.reserve(candidate.get()->objects.size());
  std::unordered_map<std::string_view, std::size_t> old_indices;
  old_indices.reserve(nodes_.size());
  for (std::size_t index = 0U; index < nodes_.size(); ++index) {
    old_indices.emplace(nodes_[index].address.source_node_uuid, index);
  }
  for (const auto& source : candidate.get()->objects) {
    runtime_node target;
    target.address = {instance_uuid_, source.uuid};
    target.name = source.name;
    if (const auto previous = old_indices.find(source.uuid); previous != old_indices.end()) {
      target.entity = nodes_[previous->second].entity;
      target.node = nodes_[previous->second].node;
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

  std::unordered_map<std::string_view, gneiss_scene_node_id> runtime_nodes;
  runtime_nodes.reserve(staged.size());
  for (const auto& target : staged) {
    if (target.node != GNEISS_NULL_SCENE_NODE_ID) {
      runtime_nodes.emplace(target.address.source_node_uuid, target.node);
    }
  }
  std::vector<std::size_t> additions;
  additions.reserve(diff.added.size());
  gneiss_entity_id active_camera_before = GNEISS_NULL_ENTITY_ID;
  (void)gneiss_world_get_active_camera(world_, &active_camera_before);
  const auto rollback_additions = [&]() noexcept {
    for (auto iterator = additions.rbegin(); iterator != additions.rend(); ++iterator) {
      auto& target = staged[*iterator];
      if (target.entity != GNEISS_NULL_ENTITY_ID) {
        (void)gneiss_world_entity_destroy(world_, target.entity);
      }
      if (target.node != GNEISS_NULL_SCENE_NODE_ID) {
        (void)gneiss_scene_node_destroy(world_, target.node);
      }
    }
    if (active_camera_before != GNEISS_NULL_ENTITY_ID) {
      (void)gneiss_world_set_active_camera(world_, active_camera_before);
    }
  };
  for (const auto& addition : diff.added) {
    const auto& source = candidate.get()->objects[addition.new_index];
    auto& target = staged[addition.new_index];
    const auto parent = source.parent_uuid ? runtime_nodes.at(*source.parent_uuid) : root_node_;
    result = gneiss_world_entity_create(world_, &target.entity);
    if (result == GNEISS_SUCCESS) {
      result = gneiss_scene_node_create(world_, parent, target.entity, &target.node);
    }
    const auto transform = to_transform(source);
    if (result == GNEISS_SUCCESS) {
      result = gneiss_scene_node_set_local_transform(world_, target.node, &transform);
    }
    if (result == GNEISS_SUCCESS) {
      result = apply_camera(world_, target.entity, source.camera);
    }
    if (result == GNEISS_SUCCESS && source.mesh_renderer) {
      const gneiss_mesh_renderer renderer{.mesh = target.mesh.get(),
                                          .material = target.material.get()};
      result = gneiss_world_entity_set_mesh_renderer(world_, target.entity, &renderer);
    }
    if (result != GNEISS_SUCCESS) {
      if (target.entity != GNEISS_NULL_ENTITY_ID) {
        (void)gneiss_world_entity_destroy(world_, target.entity);
      }
      if (target.node != GNEISS_NULL_SCENE_NODE_ID) {
        (void)gneiss_scene_node_destroy(world_, target.node);
      }
      rollback_additions();
      return result;
    }
    runtime_nodes.emplace(source.uuid, target.node);
    additions.push_back(addition.new_index);
  }

  struct update_snapshot final {
    std::size_t old_index{};
    std::size_t new_index{};
    structural_change changes{};
    gneiss_scene_node_id parent = GNEISS_NULL_SCENE_NODE_ID;
    gneiss_transform transform = GNEISS_TRANSFORM_IDENTITY;
    std::optional<camera_description> camera;
  };
  std::vector<update_snapshot> applied;
  applied.reserve(diff.updated.size());
  const auto rollback_updates = [&]() noexcept {
    for (auto iterator = applied.rbegin(); iterator != applied.rend(); ++iterator) {
      const auto& snapshot = *iterator;
      auto& target = staged[snapshot.new_index];
      if (has_change(snapshot.changes, structural_change::parent)) {
        (void)gneiss_scene_node_reparent(world_, target.node, snapshot.parent);
      }
      if (has_change(snapshot.changes, structural_change::transform)) {
        (void)gneiss_scene_node_set_local_transform(world_, target.node, &snapshot.transform);
      }
      if (has_change(snapshot.changes, structural_change::camera)) {
        (void)apply_camera(world_, target.entity, snapshot.camera);
      }
      if (has_change(snapshot.changes, structural_change::mesh_renderer)) {
        const auto& old_source = prefab_.get()->objects[snapshot.old_index];
        if (old_source.mesh_renderer) {
          const gneiss_mesh_renderer renderer{.mesh = nodes_[snapshot.old_index].mesh.get(),
                                              .material =
                                                  nodes_[snapshot.old_index].material.get()};
          (void)gneiss_world_entity_set_mesh_renderer(world_, target.entity, &renderer);
        } else {
          (void)gneiss_world_entity_remove_mesh_renderer(world_, target.entity);
        }
      }
    }
  };

  for (const auto& update : diff.updated) {
    auto& target = staged[update.new_index];
    update_snapshot snapshot{};
    snapshot.old_index = update.old_index;
    snapshot.new_index = update.new_index;
    snapshot.changes = update.changes;
    result = gneiss_scene_node_get_parent(world_, target.node, &snapshot.parent);
    if (result == GNEISS_SUCCESS) {
      result = gneiss_scene_node_get_local_transform(world_, target.node, &snapshot.transform);
    }
    if (result == GNEISS_SUCCESS && has_change(update.changes, structural_change::camera)) {
      gneiss_camera_desc camera = GNEISS_CAMERA_DESC_INIT;
      const auto camera_result = gneiss_world_entity_get_camera(world_, target.entity, &camera);
      if (camera_result == GNEISS_SUCCESS) {
        gneiss_entity_id active = GNEISS_NULL_ENTITY_ID;
        snapshot.camera = camera_description{
            .vertical_field_of_view_radians = camera.vertical_field_of_view_radians,
            .near_plane = camera.near_plane,
            .far_plane = camera.far_plane,
            .is_primary = gneiss_world_get_active_camera(world_, &active) == GNEISS_SUCCESS &&
                          active == target.entity};
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
    const auto& source = candidate.get()->objects[update.new_index];
    if (has_change(update.changes, structural_change::parent)) {
      const auto parent = source.parent_uuid ? runtime_nodes.at(*source.parent_uuid) : root_node_;
      result = gneiss_scene_node_reparent(world_, target.node, parent);
    }
    if (result == GNEISS_SUCCESS && has_change(update.changes, structural_change::transform)) {
      const auto transform = to_transform(source);
      result = gneiss_scene_node_set_local_transform(world_, target.node, &transform);
    }
    if (result == GNEISS_SUCCESS && has_change(update.changes, structural_change::camera)) {
      result = apply_camera(world_, target.entity, source.camera);
    }
    if (result == GNEISS_SUCCESS && has_change(update.changes, structural_change::mesh_renderer)) {
      if (source.mesh_renderer) {
        const gneiss_mesh_renderer renderer{.mesh = target.mesh.get(),
                                            .material = target.material.get()};
        result = gneiss_world_entity_set_mesh_renderer(world_, target.entity, &renderer);
      } else {
        result = gneiss_world_entity_remove_mesh_renderer(world_, target.entity);
      }
    }
    if (result != GNEISS_SUCCESS) {
      rollback_updates();
      rollback_additions();
      return result;
    }
  }

  auto previous_nodes = std::move(nodes_);
  nodes_ = std::move(staged);
  result = apply_overrides(registry, overrides);
  if (result != GNEISS_SUCCESS) {
    staged = std::move(nodes_);
    nodes_ = std::move(previous_nodes);
    rollback_updates();
    rollback_additions();
    return result;
  }
  for (const auto& removal : diff.removed) {
    const auto& target = previous_nodes[removal.old_index];
    (void)gneiss_world_entity_destroy(world_, target.entity);
    (void)gneiss_scene_node_destroy(world_, target.node);
  }
  prefab_ = std::move(candidate);
  loader_.release_unused();
  return GNEISS_SUCCESS;
}

gneiss_result prefab_runtime_instance::create(
    gneiss_world world, render_internal::render_asset_loader& loader, prefab_asset_lease prefab,
    gneiss_type_registry registry, std::string_view instance_uuid, gneiss_scene_node_id parent,
    const gneiss_transform& root_transform, const std::vector<prefab_property_override>& overrides,
    std::unique_ptr<prefab_runtime_instance>& out_instance) noexcept {
  out_instance.reset();
  if (world == GNEISS_NULL_WORLD || registry == GNEISS_NULL_TYPE_REGISTRY || !prefab ||
      prefab.get()->objects.empty()) {
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
    if (result == GNEISS_SUCCESS) {
      result = instance->apply_overrides(registry, overrides);
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

gneiss_result prefab_runtime_instance::apply_overrides(
    gneiss_type_registry registry,
    const std::vector<prefab_property_override>& overrides) noexcept {
  for (const auto& override_value : overrides) {
    if (override_value.key.node.instance_uuid != instance_uuid_) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    const auto validation = validate_prefab_property_override(registry, override_value);
    if (validation != GNEISS_SUCCESS) {
      return validation;
    }
    const auto found = std::ranges::find(
        nodes_, override_value.key.node.source_node_uuid,
        [](const runtime_node& node) { return std::string_view(node.address.source_node_uuid); });
    if (found == nodes_.end()) {
      return GNEISS_ERROR_NOT_FOUND;
    }
    gneiss_property_value native_value = GNEISS_PROPERTY_VALUE_INIT;
    auto result = to_native_value(override_value.value, native_value);
    if (result == GNEISS_SUCCESS) {
      const gneiss_property_target target{.context = world_, .object = found->entity};
      result = gneiss_type_registry_set_property(
          registry, override_value.key.type_id, override_value.key.field_id, target, &native_value);
    }
    if (result != GNEISS_SUCCESS) {
      return result;
    }
  }
  return GNEISS_SUCCESS;
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

gneiss_entity_id
prefab_runtime_instance::find_entity(std::string_view source_node_uuid) const noexcept {
  const auto found = std::ranges::find(nodes_, source_node_uuid, [](const runtime_node& node) {
    return std::string_view(node.address.source_node_uuid);
  });
  return found == nodes_.end() ? GNEISS_NULL_ENTITY_ID : found->entity;
}

const object_description*
prefab_runtime_instance::find_source_object(std::string_view source_node_uuid) const noexcept {
  const auto& objects = prefab_.get()->objects;
  const auto found = std::ranges::find(objects, source_node_uuid, &object_description::uuid);
  return found == objects.end() ? nullptr : &*found;
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
