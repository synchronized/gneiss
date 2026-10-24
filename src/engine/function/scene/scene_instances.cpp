// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/scene/scene_instances.hpp"
#include "engine/core/result.hpp"
#include "engine/function/scene/prefab_property_override.hpp"

#include "engine/core/identifier_conversion.hpp"
#include "engine/core/reflection/component_identity.hpp"
#include "engine/core/reflection/type_registry.hpp"

#include "engine/function/world/world_reflection.hpp"
#include "engine/function/world/world_service.hpp"

#include "engine/function/scene/scene_instance.hpp"

#include "engine/function/scene/structural_diff.hpp"
#include "engine/resource/types/scene_description.hpp"
#include "engine/resource/vfs/virtual_file_system.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>
#include <map>
#include <new>
#include <unordered_map>
#include <unordered_set>

namespace gneiss::engine {
namespace {

std::uint16_t allocate_domain() noexcept {
  static std::atomic_uint32_t next_domain{UINT32_C(1024)};
  const auto value = next_domain.fetch_add(1U, std::memory_order_relaxed);
  return value <= std::numeric_limits<std::uint16_t>::max() ? static_cast<std::uint16_t>(value)
                                                            : UINT16_C(0);
}

gneiss::engine::transform to_transform(const gneiss::engine::object_description& object) noexcept {
  gneiss::engine::transform value{};
  std::ranges::copy(object.translation, value.translation.begin());
  std::ranges::copy(object.rotation, value.rotation.begin());
  std::ranges::copy(object.scale, value.scale.begin());
  return value;
}

gneiss::engine::transform
to_transform(const gneiss::engine::prefab_instance_description& instance) noexcept {
  gneiss::engine::transform value{};
  std::ranges::copy(instance.translation, value.translation.begin());
  std::ranges::copy(instance.rotation, value.rotation.begin());
  std::ranges::copy(instance.scale, value.scale.begin());
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

gneiss::engine::result commit_object(gneiss::engine::world& native_world,
                                     const gneiss::engine::object_description& source,
                                     scene_instance::object& target, gneiss_scene_node_id parent) {
  auto result =
      gneiss::engine::write_identifier<entity_id>(target.entity, [&](auto& native_output) {
        return native_world.entity_create(native_output);
      });
  if (result == gneiss::engine::result::success) {
    result = gneiss::engine::write_identifier<scene_node_id>(target.node, [&](auto& native_output) {
      return native_world.node_create(gneiss::scene_node_id{parent},
                                      gneiss::entity_id{target.entity}, native_output);
    });
  }
  const auto transform = to_transform(source);
  if (result == gneiss::engine::result::success) {
    result = native_world.node_set_local_transform(gneiss::scene_node_id{target.node}, transform);
  }
  if (result == gneiss::engine::result::success && source.camera) {
    const gneiss::engine::camera_settings camera{.vertical_field_of_view_radians =
                                                     source.camera->vertical_field_of_view_radians,
                                                 .near_plane = source.camera->near_plane,
                                                 .far_plane = source.camera->far_plane};
    result = native_world.entity_configure_camera(gneiss::entity_id{target.entity}, camera);
    if (result == gneiss::engine::result::success && source.camera->is_primary) {
      result = native_world.set_active_camera(gneiss::entity_id{target.entity});
    }
  }
  if (result == gneiss::engine::result::success && source.mesh_renderer) {
    const gneiss::engine::mesh_renderer renderer{.mesh = gneiss::engine::asset_rid(target.mesh),
                                                 .material =
                                                     gneiss::engine::asset_rid(target.material)};
    result = native_world.entity_set_mesh_renderer(gneiss::entity_id{target.entity}, renderer);
  }
  return result;
}

[[nodiscard]] bool
equal_prefab_instances(const gneiss::engine::prefab_instance_description& left,
                       const gneiss::engine::prefab_instance_description& right) noexcept {
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

[[nodiscard]] bool equal_prefab_container(const gneiss::engine::scene_description& left,
                                          const gneiss::engine::scene_description& right) noexcept {
  return left.prefab_instances.size() == right.prefab_instances.size() &&
         std::ranges::equal(left.prefab_instances, right.prefab_instances, equal_prefab_instances);
}

gneiss::engine::result
apply_camera(gneiss::engine::world& native_world, gneiss_entity_id entity,
             const std::optional<gneiss::engine::camera_description>& camera) noexcept {
  if (!camera) {
    const auto result = native_world.entity_remove_camera(gneiss::entity_id{entity});
    return result == gneiss::engine::result::not_found ? gneiss::engine::result::success : result;
  }
  const gneiss::engine::camera_settings value{camera->vertical_field_of_view_radians,
                                              camera->near_plane, camera->far_plane};
  return native_world.entity_set_camera(gneiss::entity_id{entity}, value, camera->is_primary);
}

gneiss::engine::result apply_mesh_renderer(gneiss::engine::world& native_world,
                                           scene_instance::object& object, bool present) noexcept {
  if (!present) {
    const auto result = native_world.entity_remove_mesh_renderer(gneiss::entity_id{object.entity});
    return result == gneiss::engine::result::not_found ? gneiss::engine::result::success : result;
  }
  const gneiss::engine::mesh_renderer renderer{.mesh = gneiss::engine::asset_rid(object.mesh),
                                               .material =
                                                   gneiss::engine::asset_rid(object.material)};
  return native_world.entity_set_mesh_renderer(gneiss::entity_id{object.entity}, renderer);
}

} // namespace

gneiss::engine::result
scene_instance::stage_assets(const gneiss::engine::scene_description& description) {
  objects_.reserve(description.objects.size());
  for (const auto& source : description.objects) {
    scene_instance::object target{};
    target.uuid = source.uuid;
    if (source.mesh_renderer) {
      gneiss::engine::asset_diagnostic diagnostic;
      auto result = loader_.acquire_mesh(source.mesh_renderer->mesh_uri, target.mesh, diagnostic);
      if (result == gneiss::engine::result::success) {
        result = loader_.acquire_material(source.mesh_renderer->material_uri, target.material,
                                          diagnostic);
      }
      if (result != gneiss::engine::result::success) {
        return result;
      }
    }
    objects_.push_back(std::move(target));
  }
  return gneiss::engine::result::success;
}
gneiss::engine::result
scene_instance::commit_scene(const gneiss::engine::scene_description& description) {
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
      auto& target = objects_[index];
      const auto result = commit_object(native_world_, source, target, parent);
      if (result != gneiss::engine::result::success) {
        return result;
      }
      nodes.emplace(target.uuid, target.node);
      committed[index] = true;
      ++committed_count;
      made_progress = true;
    }
    if (!made_progress) {
      return gneiss::engine::result::internal;
    }
  }
  return gneiss::engine::result::success;
}

gneiss::engine::result
scene_instance::commit_prefabs(const gneiss::engine::scene_description& description) {
  prefab_instances_.reserve(description.prefab_instances.size());
  for (const auto& source : description.prefab_instances) {
    gneiss::engine::prefab_asset_ref lease;
    gneiss::engine::scene_diagnostic diagnostic;
    auto result = prefab_loader_.acquire(source.prefab_uri, lease, diagnostic);
    if (result != gneiss::engine::result::success) {
      return result;
    }
    const auto parent =
        source.parent_uuid ? find_node(*source.parent_uuid) : GNEISS_NULL_SCENE_NODE_ID;
    if (source.parent_uuid && parent == GNEISS_NULL_SCENE_NODE_ID) {
      return gneiss::engine::result::internal;
    }
    std::unique_ptr<prefab_runtime_instance> runtime;
    result = prefab_runtime_instance::create(
        native_world_, reflection_context_, loader_, std::move(lease), registry_,
        source.instance_uuid, parent, to_transform(source), source.overrides, runtime);
    if (result != gneiss::engine::result::success) {
      return result;
    }
    prefab_instances_.push_back(std::move(runtime));
  }
  return gneiss::engine::result::success;
}

scene_instance::scene_instance(gneiss::engine::world& native_world, gneiss_world world,
                               gneiss::engine::render_asset_loader& loader,
                               gneiss::engine::prefab_asset_loader& prefab_loader,
                               const gneiss::engine::registry_ref& registry) noexcept
    : native_world_(native_world), reflection_context_(world), loader_(loader),
      prefab_loader_(prefab_loader), registry_(registry) {}

scene_instance::~scene_instance() noexcept { rollback(); }

gneiss::engine::result scene_instance::initialize(gneiss::engine::scene_description candidate) {
  auto result = stage_assets(candidate);
  if (result == gneiss::engine::result::success) {
    result = commit_scene(candidate);
  }
  if (result == gneiss::engine::result::success) {
    result = commit_prefabs(candidate);
  }
  if (result == gneiss::engine::result::success) {
    description_ = std::move(candidate);
  }
  return result;
}
void scene_instance::append_staged_prefab(std::unique_ptr<prefab_runtime_instance> candidate) {
  prefab_instances_.push_back(std::move(candidate));
}

void scene_instance::initialize_staged(gneiss::engine::scene_description candidate) {
  description_ = std::move(candidate);
  objects_.resize(description_.objects.size());
  prefab_instances_.reserve(description_.prefab_instances.size());
}

gneiss::engine::result
scene_instance::create_staged_node(std::size_t index, gneiss_scene_node_id parent,
                                   gneiss::engine::render_asset_ref mesh,
                                   gneiss::engine::render_asset_ref material) {
  if (index >= objects_.size() || objects_[index].entity != GNEISS_NULL_ENTITY_ID) {
    return gneiss::engine::result::invalid_state;
  }
  const auto& source = description_.objects[index];
  auto& target = objects_[index];
  target.uuid = source.uuid;
  if (source.mesh_renderer) {
    if (!mesh || !material) {
      return gneiss::engine::result::not_ready;
    }
    if (gneiss::engine::asset_kind(mesh) != gneiss::engine::render_asset_type::mesh ||
        gneiss::engine::asset_kind(material) != gneiss::engine::render_asset_type::material ||
        !loader_.owns(mesh) || !loader_.owns(material)) {
      return gneiss::engine::result::invalid_argument;
    }
    target.mesh = std::move(mesh);
    target.material = std::move(material);
  }
  return commit_object(native_world_, source, target, parent);
}

void scene_instance::rollback() noexcept {
  prefab_instances_.clear();
  for (auto iterator = objects_.rbegin(); iterator != objects_.rend(); ++iterator) {
    if (iterator->entity != GNEISS_NULL_ENTITY_ID) {
      (void)native_world_.entity_destroy(gneiss::entity_id{iterator->entity});
      iterator->entity = GNEISS_NULL_ENTITY_ID;
    }
    if (iterator->node != GNEISS_NULL_SCENE_NODE_ID) {
      (void)native_world_.node_destroy(gneiss::scene_node_id{iterator->node});
      iterator->node = GNEISS_NULL_SCENE_NODE_ID;
    }
    iterator->mesh = {};
    iterator->material = {};
  }
  objects_.clear();
  loader_.release_unused();
}

gneiss_scene_node_id scene_instance::find_node(std::string_view uuid) const noexcept {
  const auto found = std::ranges::find(objects_, uuid, &object::uuid);
  return found == objects_.end() ? GNEISS_NULL_SCENE_NODE_ID : found->node;
}

gneiss::engine::result scene_instance::reload(gneiss::engine::scene_description candidate) {
  if (candidate.uuid != description_.uuid || !equal_prefab_container(description_, candidate)) {
    return gneiss::engine::result::invalid_argument;
  }

  structural_diff diff;
  auto result = build_structural_diff(description_.objects, candidate.objects, diff);
  if (result != gneiss::engine::result::success) {
    return result;
  }

  std::vector<object> staged;
  staged.reserve(candidate.objects.size());
  std::unordered_map<std::string_view, std::size_t> old_indices;
  old_indices.reserve(objects_.size());
  for (std::size_t index = 0U; index < objects_.size(); ++index) {
    old_indices.emplace(objects_[index].uuid, index);
  }
  for (const auto& source : candidate.objects) {
    object target;
    target.uuid = source.uuid;
    if (const auto previous = old_indices.find(source.uuid); previous != old_indices.end()) {
      target.entity = objects_[previous->second].entity;
      target.node = objects_[previous->second].node;
    }
    if (source.mesh_renderer) {
      gneiss::engine::asset_diagnostic diagnostic;
      result = loader_.acquire_mesh(source.mesh_renderer->mesh_uri, target.mesh, diagnostic);
      if (result == gneiss::engine::result::success) {
        result = loader_.acquire_material(source.mesh_renderer->material_uri, target.material,
                                          diagnostic);
      }
      if (result != gneiss::engine::result::success) {
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
  (void)gneiss::engine::write_identifier<entity_id>(active_camera_before, [&](auto& native_output) {
    return native_world_.get_active_camera(native_output);
  });
  const auto rollback_additions = [&]() noexcept {
    for (auto iterator = committed_additions.rbegin(); iterator != committed_additions.rend();
         ++iterator) {
      auto& target = staged[*iterator];
      if (target.entity != GNEISS_NULL_ENTITY_ID) {
        (void)native_world_.entity_destroy(gneiss::entity_id{target.entity});
      }
      if (target.node != GNEISS_NULL_SCENE_NODE_ID) {
        (void)native_world_.node_destroy(gneiss::scene_node_id{target.node});
      }
      target.entity = GNEISS_NULL_ENTITY_ID;
      target.node = GNEISS_NULL_SCENE_NODE_ID;
    }
    if (active_camera_before != GNEISS_NULL_ENTITY_ID) {
      (void)native_world_.set_active_camera(gneiss::entity_id{active_camera_before});
    }
  };
  for (const auto& addition : diff.added) {
    const auto& source = candidate.objects[addition.new_index];
    const auto parent =
        source.parent_uuid ? nodes.at(*source.parent_uuid) : GNEISS_NULL_SCENE_NODE_ID;
    result = commit_object(native_world_, source, staged[addition.new_index], parent);
    if (result != gneiss::engine::result::success) {
      auto& failed = staged[addition.new_index];
      if (failed.entity != GNEISS_NULL_ENTITY_ID) {
        (void)native_world_.entity_destroy(gneiss::entity_id{failed.entity});
      }
      if (failed.node != GNEISS_NULL_SCENE_NODE_ID) {
        (void)native_world_.node_destroy(gneiss::scene_node_id{failed.node});
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
    gneiss::engine::transform transform;
    std::optional<gneiss::engine::camera_description> camera;
  };
  std::vector<update_snapshot> applied;
  applied.reserve(diff.updated.size());
  const auto rollback_updates = [&]() noexcept {
    for (auto iterator = applied.rbegin(); iterator != applied.rend(); ++iterator) {
      const auto& snapshot = *iterator;
      auto& target = staged[snapshot.new_index];
      if (has_change(snapshot.changes, structural_change::parent)) {
        (void)native_world_.node_reparent(gneiss::scene_node_id{target.node},
                                          gneiss::scene_node_id{snapshot.parent});
      }
      if (has_change(snapshot.changes, structural_change::transform)) {
        (void)native_world_.node_set_local_transform(gneiss::scene_node_id{target.node},
                                                     snapshot.transform);
      }
      if (has_change(snapshot.changes, structural_change::camera)) {
        (void)apply_camera(native_world_, target.entity, snapshot.camera);
      }
      if (has_change(snapshot.changes, structural_change::mesh_renderer)) {
        auto& old_object = objects_[snapshot.old_index];
        (void)apply_mesh_renderer(
            native_world_, old_object,
            description_.objects[snapshot.old_index].mesh_renderer.has_value());
      }
    }
  };

  for (const auto& update : diff.updated) {
    auto& target = staged[update.new_index];
    update_snapshot snapshot{};
    snapshot.old_index = update.old_index;
    snapshot.new_index = update.new_index;
    snapshot.changes = update.changes;
    result =
        gneiss::engine::write_identifier<scene_node_id>(snapshot.parent, [&](auto& native_output) {
          return native_world_.node_get_parent(gneiss::scene_node_id{target.node}, native_output);
        });
    if (result == gneiss::engine::result::success) {
      result = native_world_.node_get_local_transform(gneiss::scene_node_id{target.node},
                                                      snapshot.transform);
    }
    if (result == gneiss::engine::result::success &&
        has_change(update.changes, structural_change::camera)) {
      gneiss::engine::camera_settings camera;
      const auto camera_result =
          native_world_.entity_get_camera(gneiss::entity_id{target.entity}, camera);
      if (camera_result == gneiss::engine::result::success) {
        gneiss_entity_id active = GNEISS_NULL_ENTITY_ID;
        const bool primary = gneiss::engine::write_identifier<entity_id>(
                                 active,
                                 [&](auto& native_output) {
                                   return native_world_.get_active_camera(native_output);
                                 }) == gneiss::engine::result::success &&
                             active == target.entity;
        snapshot.camera = gneiss::engine::camera_description{
            .vertical_field_of_view_radians = camera.vertical_field_of_view_radians,
            .near_plane = camera.near_plane,
            .far_plane = camera.far_plane,
            .is_primary = primary};
      } else if (camera_result != gneiss::engine::result::not_found) {
        result = camera_result;
      }
    }
    if (result != gneiss::engine::result::success) {
      rollback_updates();
      rollback_additions();
      return result;
    }
    applied.push_back(snapshot);

    const auto& source = candidate.objects[update.new_index];
    if (has_change(update.changes, structural_change::parent)) {
      const auto parent =
          source.parent_uuid ? nodes.at(*source.parent_uuid) : GNEISS_NULL_SCENE_NODE_ID;
      result = native_world_.node_reparent(gneiss::scene_node_id{target.node},
                                           gneiss::scene_node_id{parent});
    }
    if (result == gneiss::engine::result::success &&
        has_change(update.changes, structural_change::transform)) {
      const auto transform = to_transform(source);
      result =
          native_world_.node_set_local_transform(gneiss::scene_node_id{target.node}, transform);
    }
    if (result == gneiss::engine::result::success &&
        has_change(update.changes, structural_change::camera)) {
      result = apply_camera(native_world_, target.entity, source.camera);
    }
    if (result == gneiss::engine::result::success &&
        has_change(update.changes, structural_change::mesh_renderer)) {
      result = apply_mesh_renderer(native_world_, target, source.mesh_renderer.has_value());
    }
    if (result != gneiss::engine::result::success) {
      rollback_updates();
      rollback_additions();
      return result;
    }
  }

  for (const auto& removal : diff.removed) {
    auto& target = objects_[removal.old_index];
    result = native_world_.entity_destroy(gneiss::entity_id{target.entity});
    if (result == gneiss::engine::result::success) {
      result = native_world_.node_destroy(gneiss::scene_node_id{target.node});
    }
    if (result != gneiss::engine::result::success) {
      return result;
    }
    target.entity = GNEISS_NULL_ENTITY_ID;
    target.node = GNEISS_NULL_SCENE_NODE_ID;
  }

  objects_ = std::move(staged);
  description_ = std::move(candidate);
  loader_.release_unused();
  return gneiss::engine::result::success;
}

gneiss::engine::result scene_instance::reload_prefab(std::string_view uri) {
  std::vector<std::size_t> affected;
  affected.reserve(description_.prefab_instances.size());
  for (std::size_t index = 0U; index < description_.prefab_instances.size(); ++index) {
    if (description_.prefab_instances[index].prefab_uri == uri) {
      affected.push_back(index);
    }
  }
  if (affected.empty()) {
    return gneiss::engine::result::not_found;
  }

  gneiss::engine::prefab_asset_ref candidate;
  gneiss::engine::scene_diagnostic diagnostic;
  auto result = prefab_loader_.reload(uri, candidate, diagnostic);
  if (result != gneiss::engine::result::success) {
    return result;
  }
  for (const auto index : affected) {
    result = prefab_instances_[index]->validate_reload(
        candidate, registry_, description_.prefab_instances[index].overrides);
    if (result != gneiss::engine::result::success) {
      return result;
    }
  }
  for (const auto index : affected) {
    result = prefab_instances_[index]->reload(candidate, registry_,
                                              description_.prefab_instances[index].overrides);
    if (result != gneiss::engine::result::success) {
      return result;
    }
  }
  std::erase_if(prefab_refresh_transactions_, [&](const auto& transaction) {
    return std::ranges::any_of(affected, [&](std::size_t index) {
      return description_.prefab_instances[index].instance_uuid == transaction.instance_uuid;
    });
  });
  return gneiss::engine::result::success;
}

gneiss::engine::result scene_instance::get_node_info(std::uint64_t index, scene_node_view& out_info,
                                                     node_query_detail detail) const {
  if (index >= objects_.size()) {
    return gneiss::engine::result::not_found;
  }
  const auto& object = objects_[static_cast<std::size_t>(index)];
  gneiss_entity_id entity = GNEISS_NULL_ENTITY_ID;
  auto result = gneiss::engine::write_identifier<entity_id>(entity, [&](auto& native_output) {
    return native_world_.node_get_entity(gneiss::scene_node_id{object.node}, native_output);
  });
  if (result != gneiss::engine::result::success || entity == GNEISS_NULL_ENTITY_ID) {
    return result == gneiss::engine::result::success ? gneiss::engine::result::invalid_handle
                                                     : result;
  }
  gneiss_scene_node_id parent = GNEISS_NULL_SCENE_NODE_ID;
  result = gneiss::engine::write_identifier<scene_node_id>(parent, [&](auto& native_output) {
    return native_world_.node_get_parent(gneiss::scene_node_id{object.node}, native_output);
  });
  if (result != gneiss::engine::result::success) {
    return result;
  }
  out_info.node = scene_node_id{object.node};
  out_info.parent = scene_node_id{parent};
  out_info.entity = entity_id{entity};
  out_info.uuid = object.uuid;
  out_info.name = description_.objects[static_cast<std::size_t>(index)].name;
  if (detail != node_query_detail::identity) {
    const auto& author = description_.objects[static_cast<std::size_t>(index)];
    if (author.mesh_renderer) {
      out_info.mesh_uri = author.mesh_renderer->mesh_uri;
      out_info.material_uri = author.mesh_renderer->material_uri;
    }
  }
  if (detail == node_query_detail::components) {
    const auto& author = description_.objects[static_cast<std::size_t>(index)];
    result = native_world_.entity_get_local_transform(gneiss::entity_id{entity},
                                                      out_info.local_transform);
    if (result != gneiss::engine::result::success) {
      return result;
    }
    out_info.component_flags = author.mesh_renderer != std::nullopt
                                   ? scene_node_components::mesh_renderer
                                   : scene_node_components::none;
    if (author.camera) {
      gneiss::engine::camera_settings camera;
      result = native_world_.entity_get_camera(gneiss::entity_id{entity}, camera);
      if (result != gneiss::engine::result::success) {
        return result;
      }
      out_info.camera = gneiss::engine::camera_description{
          .vertical_field_of_view_radians = camera.vertical_field_of_view_radians,
          .near_plane = camera.near_plane,
          .far_plane = camera.far_plane,
          .is_primary = author.camera->is_primary,
      };
      out_info.component_flags |= scene_node_components::camera;
      if (author.camera->is_primary) {
        out_info.component_flags |= scene_node_components::primary_camera;
      }
    }
  }
  return gneiss::engine::result::success;
}

std::uint64_t scene_instance::get_prefab_node_count() const noexcept {
  std::uint64_t count = prefab_instances_.size();
  for (const auto& instance : prefab_instances_) {
    count += instance->node_count();
  }
  return count;
}

gneiss::engine::result scene_instance::get_prefab_node_info(std::uint64_t index,
                                                            prefab_node_view& out_info) const {
  std::uint64_t offset = 0;
  for (std::size_t instance_index = 0; instance_index < prefab_instances_.size();
       ++instance_index) {
    const auto& runtime = *prefab_instances_[instance_index];
    const auto& author = description_.prefab_instances[instance_index];
    if (index == offset) {
      gneiss_scene_node_id parent = GNEISS_NULL_SCENE_NODE_ID;
      auto result =
          gneiss::engine::write_identifier<scene_node_id>(parent, [&](auto& native_output) {
            return native_world_.node_get_parent(gneiss::scene_node_id{runtime.root()},
                                                 native_output);
          });
      if (result != gneiss::engine::result::success) {
        return result;
      }
      gneiss::engine::transform transform = {};
      result =
          native_world_.node_get_local_transform(gneiss::scene_node_id{runtime.root()}, transform);
      if (result != gneiss::engine::result::success) {
        return result;
      }
      out_info.flags = scene_prefab_flags::instance_root;
      out_info.node = scene_node_id{runtime.root()};
      out_info.parent = scene_node_id{parent};
      out_info.entity = entity_id{runtime.root_entity()};
      out_info.instance_uuid = author.instance_uuid;
      out_info.name = author.name;
      out_info.prefab_uri = author.prefab_uri;
      out_info.local_transform = transform;
      return gneiss::engine::result::success;
    }
    ++offset;
    if (index < offset + runtime.node_count()) {
      prefab_runtime_instance::node_info source;
      const auto result = runtime.get_node_info(static_cast<std::size_t>(index - offset), source);
      if (result != gneiss::engine::result::success) {
        return result;
      }
      out_info.flags = scene_prefab_flags::source_read_only;
      out_info.node = scene_node_id{source.node};
      out_info.parent = scene_node_id{source.parent};
      out_info.entity = entity_id{source.entity};
      out_info.instance_uuid = source.address->instance_uuid;
      out_info.source_node_uuid = source.address->source_node_uuid;
      out_info.name = source.name;
      out_info.prefab_uri = author.prefab_uri;
      const auto* source_object = runtime.find_source_object(source.address->source_node_uuid);
      if (source_object == nullptr) {
        return gneiss::engine::result::invalid_state;
      }
      out_info.source_local_transform = to_transform(*source_object);
      const auto transform_type = gneiss::engine::transform_component_type;
      for (const auto& override_value : author.overrides) {
        if (override_value.key.node.source_node_uuid != source.address->source_node_uuid ||
            !std::ranges::equal(override_value.key.type_id.value.bytes(),
                                transform_type.value.bytes())) {
          continue;
        }
        switch (override_value.key.field_id.get()) {
        case GNEISS_TRANSFORM_FIELD_TRANSLATION:
          out_info.flags |= scene_prefab_flags::translation_overridden;
          break;
        case GNEISS_TRANSFORM_FIELD_ROTATION:
          out_info.flags |= scene_prefab_flags::rotation_overridden;
          break;
        case GNEISS_TRANSFORM_FIELD_SCALE:
          out_info.flags |= scene_prefab_flags::scale_overridden;
          break;
        default:
          break;
        }
      }
      const auto transform_result = native_world_.node_get_local_transform(
          gneiss::scene_node_id{source.node}, out_info.local_transform);
      return transform_result;
    }
    offset += runtime.node_count();
  }
  return gneiss::engine::result::not_found;
}

gneiss::engine::result
scene_instances::capture_prefab_header(gneiss_scene_instance instance, std::string_view uuid,
                                       gneiss::engine::prefab_instance_description& author,
                                       gneiss::engine::prefab_asset_ref& source) const noexcept {
  if (!native_world_.is_owner_thread()) {
    return gneiss::engine::result::invalid_state;
  }
  auto* slot = instances_.get(instance, gneiss::engine::resource_type::scene_instance);
  if (!slot || !*slot) {
    return gneiss::engine::result::invalid_handle;
  }
  return (*slot)->capture_prefab_instance_header(uuid, author, source);
}

gneiss::engine::result
scene_instances::capture_prefab_binding(gneiss_scene_instance instance, std::string_view uuid,
                                        std::size_t index, mesh_binding& output) const noexcept {
  if (!native_world_.is_owner_thread()) {
    return gneiss::engine::result::invalid_state;
  }
  auto* slot = instances_.get(instance, gneiss::engine::resource_type::scene_instance);
  if (!slot || !*slot) {
    return gneiss::engine::result::invalid_handle;
  }
  return (*slot)->capture_prefab_instance_binding(uuid, index, output);
}

gneiss::engine::result scene_instances::begin_prefab_candidate(
    gneiss_scene_instance instance, const gneiss::engine::prefab_instance_description& author,
    gneiss::engine::prefab_asset_ref prefab, gneiss::engine::prefab_description prepared,
    prefab_candidate& output, bool replacing) noexcept {
  if (output.is_valid()) {
    return gneiss::engine::result::invalid_state;
  }
  if (!native_world_.is_owner_thread()) {
    return gneiss::engine::result::invalid_state;
  }
  auto* slot = instances_.get(instance, gneiss::engine::resource_type::scene_instance);
  if (!slot || !*slot) {
    return gneiss::engine::result::invalid_handle;
  }
  return (*slot)->prepare_prefab_instance_candidate(instance, author, std::move(prefab), prepared,
                                                    output, replacing);
}

gneiss::engine::result
scene_instances::commit_prefab_candidate(gneiss_scene_instance instance,
                                         prefab_candidate& candidate) noexcept {
  if (!native_world_.is_owner_thread()) {
    return gneiss::engine::result::invalid_state;
  }
  auto* slot = instances_.get(instance, gneiss::engine::resource_type::scene_instance);
  if (!slot || !*slot) {
    return gneiss::engine::result::invalid_handle;
  }
  return (*slot)->publish_prefab_instance_candidate(instance, candidate);
}

gneiss::engine::result scene_instances::commit_prefab_replacements(
    gneiss_scene_instance instance, std::span<prefab_candidate* const> replacements) noexcept {
  if (!native_world_.is_owner_thread()) {
    return gneiss::engine::result::invalid_state;
  }
  auto* slot = instances_.get(instance, gneiss::engine::resource_type::scene_instance);
  if (!slot || !*slot) {
    return gneiss::engine::result::invalid_handle;
  }
  return (*slot)->replace_prefab_instance_versions(instance, replacements);
}

gneiss::engine::result scene_instances::prepare_prefab_removal(gneiss_scene_instance instance,
                                                               std::string_view uuid,
                                                               bool& complete) noexcept {
  complete = false;
  if (!native_world_.is_owner_thread()) {
    return gneiss::engine::result::invalid_state;
  }
  auto* slot = instances_.get(instance, gneiss::engine::resource_type::scene_instance);
  if (!slot || !*slot) {
    return gneiss::engine::result::invalid_handle;
  }
  return (*slot)->prepare_prefab_instance_removal(uuid, complete);
}

gneiss::engine::result
scene_instances::commit_prefab_removal(gneiss_scene_instance instance, std::string_view uuid,
                                       prefab_candidate& candidate) noexcept {
  if (candidate.is_valid()) {
    return gneiss::engine::result::invalid_state;
  }
  if (!native_world_.is_owner_thread()) {
    return gneiss::engine::result::invalid_state;
  }
  auto* slot = instances_.get(instance, gneiss::engine::resource_type::scene_instance);
  if (!slot || !*slot) {
    return gneiss::engine::result::invalid_handle;
  }
  return (*slot)->remove_prefab_instance(uuid, candidate);
}

gneiss::engine::result scene_instance::create_prefab_instance(const prefab_creation& desc,
                                                              gneiss_scene_node_id* out_root) {
  *out_root = GNEISS_NULL_SCENE_NODE_ID;
  const auto instance_uuid = desc.instance_uuid;
  const auto name = desc.name;
  const auto prefab_uri = desc.prefab_uri;
  if (!is_canonical_uuid(instance_uuid) ||
      std::ranges::any_of(
          objects_, [instance_uuid](const auto& object) { return object.uuid == instance_uuid; }) ||
      std::ranges::any_of(description_.prefab_instances, [instance_uuid](const auto& instance) {
        return instance.instance_uuid == instance_uuid;
      })) {
    return gneiss::engine::result::invalid_argument;
  }
  try {
    std::optional<std::string> parent_uuid;
    if (desc.parent.get() != GNEISS_NULL_SCENE_NODE_ID) {
      const auto found = std::ranges::find(objects_, desc.parent.get(), &object::node);
      if (found == objects_.end()) {
        return gneiss::engine::result::invalid_handle;
      }
      parent_uuid = found->uuid;
    }
    gneiss::engine::prefab_asset_ref lease;
    gneiss::engine::scene_diagnostic diagnostic;
    auto result = prefab_loader_.acquire(prefab_uri, lease, diagnostic);
    if (result != gneiss::engine::result::success) {
      return result;
    }
    gneiss::engine::prefab_instance_description author{.instance_uuid = std::string(instance_uuid),
                                                       .name = std::string(name),
                                                       .parent_uuid = std::move(parent_uuid),
                                                       .prefab_uri = std::string(prefab_uri),
                                                       .overrides = {}};
    std::ranges::copy(desc.local_transform.translation, author.translation.begin());
    std::ranges::copy(desc.local_transform.rotation, author.rotation.begin());
    std::ranges::copy(desc.local_transform.scale, author.scale.begin());
    description_.prefab_instances.reserve(description_.prefab_instances.size() + 1U);
    prefab_instances_.reserve(prefab_instances_.size() + 1U);
    std::unique_ptr<prefab_runtime_instance> runtime;
    result = prefab_runtime_instance::create(
        native_world_, reflection_context_, loader_, std::move(lease), registry_, instance_uuid,
        desc.parent.get(), desc.local_transform, author.overrides, runtime);
    if (result != gneiss::engine::result::success) {
      return result;
    }
    *out_root = runtime->root();
    description_.prefab_instances.push_back(std::move(author));
    prefab_instances_.push_back(std::move(runtime));
    return gneiss::engine::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

gneiss::engine::result scene_instance::set_prefab_instance_name(gneiss_scene_node_id root,
                                                                std::string_view name) {
  const auto found = std::ranges::find_if(
      prefab_instances_, [root](const auto& instance) { return instance->root() == root; });
  if (found == prefab_instances_.end()) {
    return gneiss::engine::result::invalid_handle;
  }
  try {
    std::string prepared(name);
    description_.prefab_instances[static_cast<std::size_t>(found - prefab_instances_.begin())]
        .name.swap(prepared);
    return gneiss::engine::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

gneiss::engine::result
scene_instance::set_prefab_source_transform(gneiss_scene_node_id node,
                                            const gneiss::engine::transform& transform) {
  for (std::size_t index = 0U; index < prefab_instances_.size(); ++index) {
    auto& runtime = *prefab_instances_[index];
    prefab_runtime_instance::node_info info;
    for (std::size_t node_index = 0U; node_index < runtime.node_count(); ++node_index) {
      if (runtime.get_node_info(node_index, info) != gneiss::engine::result::success ||
          info.node != node) {
        continue;
      }
      const auto* source = runtime.find_source_object(info.address->source_node_uuid);
      if (source == nullptr) {
        return gneiss::engine::result::invalid_state;
      }
      auto pending = description_.prefab_instances[index].overrides;
      const auto type_id = gneiss::engine::transform_component_type;
      const std::array candidates{
          gneiss::engine::prefab_property_override{
              .key = {.node = *info.address,
                      .type_id = type_id,
                      .field_id = gneiss::field_id{GNEISS_TRANSFORM_FIELD_TRANSLATION}},
              .value = {.payload = transform.translation}},
          gneiss::engine::prefab_property_override{
              .key = {.node = *info.address,
                      .type_id = type_id,
                      .field_id = gneiss::field_id{GNEISS_TRANSFORM_FIELD_ROTATION}},
              .value = {.payload = transform.rotation}},
          gneiss::engine::prefab_property_override{
              .key = {.node = *info.address,
                      .type_id = type_id,
                      .field_id = gneiss::field_id{GNEISS_TRANSFORM_FIELD_SCALE}},
              .value = {.payload = transform.scale}}};
      const std::array source_values{
          gneiss::engine::prefab_property_value{.payload = source->translation},
          gneiss::engine::prefab_property_value{.payload = source->rotation},
          gneiss::engine::prefab_property_value{.payload = source->scale}};
      for (std::size_t field_index = 0U; field_index < candidates.size(); ++field_index) {
        const auto result = set_prefab_property_override(
            registry_, pending, candidates[field_index], source_values[field_index]);
        if (result != gneiss::engine::result::success) {
          return result;
        }
      }
      const auto result =
          native_world_.node_set_local_transform(gneiss::scene_node_id{node}, transform);
      if (result != gneiss::engine::result::success) {
        return result;
      }
      description_.prefab_instances[index].overrides.swap(pending);
      return gneiss::engine::result::success;
    }
  }
  return gneiss::engine::result::invalid_handle;
}

gneiss::engine::result scene_instance::destroy_prefab_instance(gneiss_scene_node_id root) noexcept {
  const auto found = std::ranges::find_if(
      prefab_instances_, [root](const auto& instance) { return instance->root() == root; });
  if (found == prefab_instances_.end()) {
    return gneiss::engine::result::invalid_handle;
  }
  const auto index = static_cast<std::size_t>(found - prefab_instances_.begin());
  prefab_instances_.erase(found);
  description_.prefab_instances.erase(description_.prefab_instances.begin() +
                                      static_cast<std::ptrdiff_t>(index));
  return gneiss::engine::result::success;
}

gneiss::engine::result
scene_instance::refresh_prefab_instance(gneiss_scene_node_id root,
                                        gneiss_scene_node_id* out_new_root,
                                        gneiss_scene_prefab_refresh_token* out_token) {
  *out_new_root = GNEISS_NULL_SCENE_NODE_ID;
  *out_token = GNEISS_NULL_SCENE_PREFAB_REFRESH_TOKEN;
  const auto found = std::ranges::find_if(
      prefab_instances_, [root](const auto& instance) { return instance->root() == root; });
  if (found == prefab_instances_.end()) {
    return gneiss::engine::result::invalid_handle;
  }
  const auto index = static_cast<std::size_t>(found - prefab_instances_.begin());
  const auto& author = description_.prefab_instances[index];
  gneiss_scene_node_id parent = GNEISS_NULL_SCENE_NODE_ID;
  auto result = gneiss::engine::write_identifier<scene_node_id>(parent, [&](auto& native_output) {
    return native_world_.node_get_parent(gneiss::scene_node_id{root}, native_output);
  });
  gneiss::engine::transform transform = {};
  if (result == gneiss::engine::result::success) {
    result = native_world_.node_get_local_transform(gneiss::scene_node_id{root}, transform);
  }
  if (result != gneiss::engine::result::success) {
    return result;
  }
  if (next_prefab_refresh_token_ == GNEISS_NULL_SCENE_PREFAB_REFRESH_TOKEN) {
    return gneiss::engine::result::out_of_memory;
  }
  prefab_refresh_transactions_.reserve(prefab_refresh_transactions_.size() + 1U);
  auto old_lease = (*found)->prefab_lease();
  gneiss::engine::prefab_asset_ref lease;
  gneiss::engine::scene_diagnostic diagnostic;
  result = prefab_loader_.reload(author.prefab_uri, lease, diagnostic);
  if (result != gneiss::engine::result::success) {
    return result;
  }
  std::unique_ptr<prefab_runtime_instance> replacement;
  result = prefab_runtime_instance::create(native_world_, reflection_context_, loader_,
                                           std::move(lease), registry_, author.instance_uuid,
                                           parent, transform, author.overrides, replacement);
  if (result != gneiss::engine::result::success) {
    return result;
  }
  const auto token = next_prefab_refresh_token_++;
  prefab_refresh_transactions_.push_back(
      {.token = token, .instance_uuid = author.instance_uuid, .alternate = std::move(old_lease)});
  *out_new_root = replacement->root();
  *out_token = token;
  *found = std::move(replacement);
  return gneiss::engine::result::success;
}

gneiss::engine::result
scene_instance::toggle_prefab_refresh(gneiss_scene_prefab_refresh_token token,
                                      gneiss_scene_node_id* out_new_root) {
  *out_new_root = GNEISS_NULL_SCENE_NODE_ID;
  const auto transaction =
      std::ranges::find(prefab_refresh_transactions_, token, &prefab_refresh_transaction::token);
  if (transaction == prefab_refresh_transactions_.end()) {
    return gneiss::engine::result::invalid_handle;
  }
  const auto author =
      std::ranges::find(description_.prefab_instances, transaction->instance_uuid,
                        &gneiss::engine::prefab_instance_description::instance_uuid);
  if (author == description_.prefab_instances.end()) {
    return gneiss::engine::result::invalid_state;
  }
  const auto index = static_cast<std::size_t>(author - description_.prefab_instances.begin());
  if (index >= prefab_instances_.size()) {
    return gneiss::engine::result::internal;
  }
  auto& current = prefab_instances_[index];
  gneiss_scene_node_id parent = GNEISS_NULL_SCENE_NODE_ID;
  auto result = gneiss::engine::write_identifier<scene_node_id>(parent, [&](auto& native_output) {
    return native_world_.node_get_parent(gneiss::scene_node_id{current->root()}, native_output);
  });
  gneiss::engine::transform transform = {};
  if (result == gneiss::engine::result::success) {
    result =
        native_world_.node_get_local_transform(gneiss::scene_node_id{current->root()}, transform);
  }
  if (result != gneiss::engine::result::success) {
    return result;
  }
  auto current_lease = current->prefab_lease();
  std::unique_ptr<prefab_runtime_instance> replacement;
  result = prefab_runtime_instance::create(native_world_, reflection_context_, loader_,
                                           transaction->alternate, registry_, author->instance_uuid,
                                           parent, transform, author->overrides, replacement);
  if (result != gneiss::engine::result::success) {
    return result;
  }
  transaction->alternate = std::move(current_lease);
  *out_new_root = replacement->root();
  current = std::move(replacement);
  return gneiss::engine::result::success;
}

gneiss::engine::result
scene_instance::release_prefab_refresh(gneiss_scene_prefab_refresh_token token) noexcept {
  const auto found =
      std::ranges::find(prefab_refresh_transactions_, token, &prefab_refresh_transaction::token);
  if (found == prefab_refresh_transactions_.end()) {
    return gneiss::engine::result::invalid_handle;
  }
  prefab_refresh_transactions_.erase(found);
  return gneiss::engine::result::success;
}

gneiss::engine::result scene_instance::create_node(const node_creation& desc,
                                                   gneiss_scene_node_id* out_node) {
  const auto uuid = desc.uuid;
  const auto name = desc.name;
  if (!is_canonical_uuid(uuid) || find_node(uuid) != GNEISS_NULL_SCENE_NODE_ID) {
    return gneiss::engine::result::invalid_argument;
  }
  std::optional<std::string> parent_uuid;
  if (desc.parent.get() != GNEISS_NULL_SCENE_NODE_ID) {
    const auto parent = std::ranges::find(objects_, desc.parent.get(), &object::node);
    if (parent == objects_.end()) {
      return gneiss::engine::result::invalid_handle;
    }
    parent_uuid = parent->uuid;
  }
  try {
    gneiss::engine::object_description author{.uuid = std::string(uuid),
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
    objects_.reserve(objects_.size() + 1U);
    description_.objects.reserve(description_.objects.size() + 1U);
    object target{.uuid = author.uuid,
                  .entity = GNEISS_NULL_ENTITY_ID,
                  .node = GNEISS_NULL_SCENE_NODE_ID,
                  .mesh = {},
                  .material = {}};
    const auto result = commit_object(native_world_, author, target, desc.parent.get());
    if (result != gneiss::engine::result::success) {
      if (target.entity != GNEISS_NULL_ENTITY_ID) {
        (void)native_world_.entity_destroy(gneiss::entity_id{target.entity});
      }
      if (target.node != GNEISS_NULL_SCENE_NODE_ID) {
        (void)native_world_.node_destroy(gneiss::scene_node_id{target.node});
      }
      return result;
    }
    *out_node = target.node;
    objects_.push_back(std::move(target));
    description_.objects.push_back(std::move(author));
    return gneiss::engine::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

gneiss::engine::result scene_instance::set_node_name(gneiss_scene_node_id node,
                                                     std::string_view name) {
  const auto found = std::ranges::find(objects_, node, &object::node);
  if (found == objects_.end()) {
    return gneiss::engine::result::invalid_handle;
  }
  try {
    std::string author_name(name);
    const auto index = static_cast<std::size_t>(std::distance(objects_.begin(), found));
    description_.objects[index].name.swap(author_name);
    return gneiss::engine::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  }
}

gneiss::engine::result scene_instance::reparent_node(gneiss_scene_node_id node,
                                                     gneiss_scene_node_id parent) {
  const auto found = std::ranges::find(objects_, node, &object::node);
  if (found == objects_.end()) {
    return gneiss::engine::result::invalid_handle;
  }
  std::optional<std::string> parent_uuid;
  try {
    if (parent != GNEISS_NULL_SCENE_NODE_ID) {
      const auto parent_object = std::ranges::find(objects_, parent, &object::node);
      if (parent_object == objects_.end()) {
        return gneiss::engine::result::invalid_handle;
      }
      parent_uuid = parent_object->uuid;
    }
    const auto result =
        native_world_.node_reparent(gneiss::scene_node_id{node}, gneiss::scene_node_id{parent});
    if (result != gneiss::engine::result::success) {
      return result;
    }
    const auto index = static_cast<std::size_t>(std::distance(objects_.begin(), found));
    description_.objects[index].parent_uuid.swap(parent_uuid);
    return gneiss::engine::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  }
}

gneiss::engine::result scene_instance::capture_subtree(gneiss_scene_node_id root,
                                                       std::string& out_snapshot) const {
  const auto found = std::ranges::find(objects_, root, &object::node);
  if (found == objects_.end()) {
    return gneiss::engine::result::invalid_handle;
  }
  try {
    std::string current_json;
    auto result = serialize(current_json);
    if (result != gneiss::engine::result::success) {
      return result;
    }
    gneiss::engine::scene_description current;
    gneiss::engine::scene_diagnostic diagnostic;
    result = gneiss::engine::parse_scene_description(current_json, current, diagnostic);
    if (result != gneiss::engine::result::success) {
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
      return gneiss::engine::result::unsupported;
    }
    // 当前子树恢复只处理普通作者节点；不能生成会遗漏 Prefab 的撤销快照。
    if (std::ranges::any_of(current.prefab_instances, [&included](const auto& instance) {
          return instance.parent_uuid && included.contains(*instance.parent_uuid);
        })) {
      return gneiss::engine::result::unsupported;
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
    return gneiss::engine::serialize_scene_description(current, out_snapshot);
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

gneiss::engine::result scene_instance::restore_subtree(std::string_view snapshot,
                                                       gneiss_scene_node_id parent,
                                                       std::span<const uuid_mapping> mappings,
                                                       gneiss_scene_node_id* out_root) {
  gneiss::engine::scene_description subtree;
  gneiss::engine::scene_diagnostic diagnostic;
  auto result = gneiss::engine::parse_scene_description(snapshot, subtree, diagnostic);
  if (result != gneiss::engine::result::success || subtree.objects.empty()) {
    return result == gneiss::engine::result::success ? gneiss::engine::result::invalid_argument
                                                     : result;
  }
  if (!subtree.prefab_instances.empty()) {
    return gneiss::engine::result::unsupported;
  }
  if (subtree.objects.size() > GNEISS_SCENE_SUBTREE_MAX_NODES) {
    return gneiss::engine::result::unsupported;
  }
  std::optional<std::string> external_parent_uuid;
  if (parent != GNEISS_NULL_SCENE_NODE_ID) {
    const auto found = std::ranges::find(objects_, parent, &object::node);
    if (found == objects_.end()) {
      return gneiss::engine::result::invalid_handle;
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
          return gneiss::engine::result::invalid_argument;
        }
        root = iterator;
      }
    }
    if (root == subtree.objects.end()) {
      return gneiss::engine::result::invalid_argument;
    }
    const auto source_root_uuid = root->uuid;
    std::unordered_map<std::string, std::string> uuid_map;
    if (!mappings.empty()) {
      if (mappings.size() != subtree.objects.size()) {
        return gneiss::engine::result::invalid_argument;
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
          return gneiss::engine::result::invalid_argument;
        }
      }
    } else {
      for (const auto& author : subtree.objects) {
        if (find_node(author.uuid) != GNEISS_NULL_SCENE_NODE_ID) {
          return gneiss::engine::result::invalid_argument;
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
                    .entity = GNEISS_NULL_ENTITY_ID,
                    .node = GNEISS_NULL_SCENE_NODE_ID,
                    .mesh = {},
                    .material = {}};
      if (author.mesh_renderer) {
        gneiss::engine::asset_diagnostic asset_diagnostic;
        result =
            loader_.acquire_mesh(author.mesh_renderer->mesh_uri, target.mesh, asset_diagnostic);
        if (result == gneiss::engine::result::success) {
          result = loader_.acquire_material(author.mesh_renderer->material_uri, target.material,
                                            asset_diagnostic);
        }
        if (result != gneiss::engine::result::success) {
          return result;
        }
      }
      staged.push_back(std::move(target));
    }
    objects_.reserve(objects_.size() + staged.size());
    description_.objects.reserve(description_.objects.size() + subtree.objects.size());
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
        result = commit_object(native_world_, author, staged[index], runtime_parent);
        if (result != gneiss::engine::result::success) {
          for (auto& value : staged) {
            if (value.entity != GNEISS_NULL_ENTITY_ID) {
              (void)native_world_.entity_destroy(gneiss::entity_id{value.entity});
            }
            if (value.node != GNEISS_NULL_SCENE_NODE_ID) {
              (void)native_world_.node_destroy(gneiss::scene_node_id{value.node});
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
        return gneiss::engine::result::invalid_argument;
      }
    }
    const auto restored_root_uuid =
        uuid_map.empty() ? source_root_uuid : uuid_map.at(source_root_uuid);
    *out_root = committed_nodes.at(restored_root_uuid);
    for (auto& value : staged) {
      objects_.push_back(std::move(value));
    }
    for (auto& author : subtree.objects) {
      description_.objects.push_back(std::move(author));
    }
    return gneiss::engine::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

gneiss::engine::result scene_instance::destroy_subtree(gneiss_scene_node_id root) {
  const auto found = std::ranges::find(objects_, root, &object::node);
  if (found == objects_.end()) {
    return gneiss::engine::result::invalid_handle;
  }
  try {
    std::unordered_set<std::string> removed{found->uuid};
    bool changed = true;
    while (changed) {
      changed = false;
      for (const auto& author : description_.objects) {
        if (author.parent_uuid && removed.contains(*author.parent_uuid) &&
            removed.emplace(author.uuid).second) {
          changed = true;
        }
      }
    }
    if (std::ranges::any_of(description_.prefab_instances, [&removed](const auto& instance) {
          return instance.parent_uuid && removed.contains(*instance.parent_uuid);
        })) {
      return gneiss::engine::result::unsupported;
    }
    std::vector<std::pair<object*, std::size_t>> ordered;
    ordered.reserve(removed.size());
    for (auto& value : objects_) {
      if (!removed.contains(value.uuid)) {
        continue;
      }
      if (gneiss::engine::write_identifier<entity_id>(value.entity, [&](auto& native_output) {
            return native_world_.node_get_entity(gneiss::scene_node_id{value.node}, native_output);
          }) != gneiss::engine::result::success) {
        return gneiss::engine::result::invalid_handle;
      }
      std::size_t depth = 0U;
      auto current_uuid = std::string_view{value.uuid};
      while (true) {
        const auto author = std::ranges::find(description_.objects, current_uuid,
                                              &gneiss::engine::object_description::uuid);
        if (author == description_.objects.end() || !author->parent_uuid ||
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
      auto destroy_result = native_world_.entity_destroy(gneiss::entity_id{value->entity});
      if (destroy_result != gneiss::engine::result::success) {
        return destroy_result;
      }
      destroy_result = native_world_.node_destroy(gneiss::scene_node_id{value->node});
      if (destroy_result != gneiss::engine::result::success) {
        return destroy_result;
      }
    }
    std::erase_if(objects_, [&removed](const auto& value) { return removed.contains(value.uuid); });
    std::erase_if(description_.objects,
                  [&removed](const auto& value) { return removed.contains(value.uuid); });
    loader_.release_unused();
    return gneiss::engine::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  }
}

gneiss::engine::result
scene_instance::create_mesh_renderer_node(const mesh_renderer_node_creation& desc,
                                          gneiss_scene_node_id* out_node,
                                          const scene_mesh_binding* prepared) {
  using asset_loader = gneiss::engine::render_asset_loader;
  if (prepared &&
      (gneiss::engine::asset_kind(prepared->mesh) != gneiss::engine::render_asset_type::mesh ||
       gneiss::engine::asset_kind(prepared->material) !=
           gneiss::engine::render_asset_type::material ||
       !loader_.owns(prepared->mesh) || !loader_.owns(prepared->material) ||
       gneiss::engine::asset_uri(prepared->mesh) != desc.mesh_uri ||
       gneiss::engine::asset_uri(prepared->material) != desc.material_uri)) {
    return gneiss::engine::result::invalid_argument;
  }
  const auto uuid = desc.uuid;
  const auto name = desc.name;
  const auto mesh_uri = desc.mesh_uri;
  const auto material_uri = desc.material_uri;
  if (!is_canonical_uuid(uuid) || find_node(uuid) != GNEISS_NULL_SCENE_NODE_ID) {
    return gneiss::engine::result::invalid_argument;
  }
  std::optional<std::string> parent_uuid;
  if (desc.parent.get() != GNEISS_NULL_SCENE_NODE_ID) {
    const auto parent = std::ranges::find(objects_, desc.parent.get(), &object::node);
    if (parent == objects_.end()) {
      return gneiss::engine::result::invalid_handle;
    }
    parent_uuid = parent->uuid;
  }

  try {
    gneiss::engine::object_description author{
        .uuid = std::string(uuid),
        .name = std::string(name),
        .parent_uuid = std::move(parent_uuid),
        .translation = {},
        .rotation = {0.0F, 0.0F, 0.0F, 1.0F},
        .scale = {1.0F, 1.0F, 1.0F},
        .camera = std::nullopt,
        .mesh_renderer = gneiss::engine::mesh_renderer_description{
            .mesh_uri = std::string(mesh_uri), .material_uri = std::string(material_uri)}};
    objects_.reserve(objects_.size() + 1U);
    description_.objects.reserve(description_.objects.size() + 1U);
    object target{};
    target.uuid = author.uuid;
    gneiss::engine::asset_diagnostic diagnostic;
    auto result = gneiss::engine::result::success;
    if (prepared) {
      target.mesh = asset_loader::mesh_ref(prepared->mesh);
      target.material = asset_loader::material_ref(prepared->material);
    } else {
      result = loader_.acquire_mesh(mesh_uri, target.mesh, diagnostic);
      if (result == gneiss::engine::result::success) {
        result = loader_.acquire_material(material_uri, target.material, diagnostic);
      }
    }
    if (result != gneiss::engine::result::success) {
      return result;
    }
    result = commit_object(native_world_, author, target, desc.parent.get());
    if (result != gneiss::engine::result::success) {
      if (target.entity != GNEISS_NULL_ENTITY_ID) {
        (void)native_world_.entity_destroy(gneiss::entity_id{target.entity});
      }
      if (target.node != GNEISS_NULL_SCENE_NODE_ID) {
        (void)native_world_.node_destroy(gneiss::scene_node_id{target.node});
      }
      return result;
    }
    *out_node = target.node;
    objects_.push_back(std::move(target));
    description_.objects.push_back(std::move(author));
    return gneiss::engine::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

gneiss::engine::result scene_instance::set_mesh_renderer(gneiss_scene_node_id node,
                                                         std::string_view mesh_uri,
                                                         std::string_view material_uri) {
  const auto found = std::ranges::find(objects_, node, &object::node);
  if (found == objects_.end()) {
    return gneiss::engine::result::invalid_handle;
  }
  try {
    gneiss::engine::mesh_renderer_description author{.mesh_uri = std::string(mesh_uri),
                                                     .material_uri = std::string(material_uri)};
    // 编辑资产选择不等于重载；未改变的 URI 继续使用节点选定的版本。
    auto mesh = gneiss::engine::asset_uri(found->mesh) == mesh_uri
                    ? found->mesh
                    : gneiss::engine::render_asset_ref{};
    auto material = gneiss::engine::asset_uri(found->material) == material_uri
                        ? found->material
                        : gneiss::engine::render_asset_ref{};
    gneiss::engine::asset_diagnostic diagnostic;
    auto result = gneiss::engine::result::success;
    if (!mesh) {
      result = loader_.acquire_mesh(mesh_uri, mesh, diagnostic);
    }
    if (result == gneiss::engine::result::success && !material) {
      result = loader_.acquire_material(material_uri, material, diagnostic);
    }
    if (result != gneiss::engine::result::success) {
      return result;
    }
    const gneiss::engine::mesh_renderer renderer{.mesh = gneiss::engine::asset_rid(mesh),
                                                 .material = gneiss::engine::asset_rid(material)};
    result = native_world_.entity_set_mesh_renderer(gneiss::entity_id{found->entity}, renderer);
    if (result != gneiss::engine::result::success) {
      return result;
    }
    const auto index = static_cast<std::size_t>(std::distance(objects_.begin(), found));
    description_.objects[index].mesh_renderer = std::move(author);
    found->mesh = std::move(mesh);
    found->material = std::move(material);
    loader_.release_unused();
    return gneiss::engine::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

gneiss::engine::result scene_instance::set_camera(gneiss_scene_node_id node,
                                                  const gneiss::engine::camera_description& desc) {
  const auto found = std::ranges::find(objects_, node, &object::node);
  if (found == objects_.end()) {
    return gneiss::engine::result::invalid_handle;
  }
  const auto result = native_world_.entity_configure_camera(
      gneiss::entity_id{found->entity},
      {.vertical_field_of_view_radians = desc.vertical_field_of_view_radians,
       .near_plane = desc.near_plane,
       .far_plane = desc.far_plane});
  if (result != gneiss::engine::result::success) {
    return result;
  }
  if (desc.is_primary) {
    const auto active_result = native_world_.set_active_camera(gneiss::entity_id{found->entity});
    if (active_result != gneiss::engine::result::success) {
      return active_result;
    }
  }
  const auto index = static_cast<std::size_t>(std::distance(objects_.begin(), found));
  if (desc.is_primary) {
    for (auto& author : description_.objects) {
      if (author.camera) {
        author.camera->is_primary = false;
      }
    }
  }
  description_.objects[index].camera = desc;
  return gneiss::engine::result::success;
}

gneiss::engine::result scene_instance::remove_camera(gneiss_scene_node_id node) {
  const auto found = std::ranges::find(objects_, node, &object::node);
  if (found == objects_.end()) {
    return gneiss::engine::result::invalid_handle;
  }
  const auto index = static_cast<std::size_t>(std::distance(objects_.begin(), found));
  if (!description_.objects[index].camera) {
    return gneiss::engine::result::not_found;
  }
  const auto result = native_world_.entity_remove_camera(gneiss::entity_id{found->entity});
  if (result == gneiss::engine::result::success) {
    description_.objects[index].camera.reset();
  }
  return result;
}

gneiss::engine::result scene_instance::remove_mesh_renderer(gneiss_scene_node_id node) {
  const auto found = std::ranges::find(objects_, node, &object::node);
  if (found == objects_.end()) {
    return gneiss::engine::result::invalid_handle;
  }
  const auto index = static_cast<std::size_t>(std::distance(objects_.begin(), found));
  if (!description_.objects[index].mesh_renderer) {
    return gneiss::engine::result::not_found;
  }
  const auto result = native_world_.entity_remove_mesh_renderer(gneiss::entity_id{found->entity});
  if (result != gneiss::engine::result::success) {
    return result;
  }
  description_.objects[index].mesh_renderer.reset();
  found->mesh = {};
  found->material = {};
  loader_.release_unused();
  return gneiss::engine::result::success;
}

gneiss::engine::result scene_instance::destroy_node(gneiss_scene_node_id node) {
  const auto found = std::ranges::find(objects_, node, &object::node);
  if (found == objects_.end()) {
    return gneiss::engine::result::invalid_handle;
  }
  const auto index = static_cast<std::size_t>(std::distance(objects_.begin(), found));
  const auto& uuid = description_.objects[index].uuid;
  if (std::ranges::any_of(description_.objects,
                          [&uuid](const auto& candidate) {
                            return candidate.parent_uuid && *candidate.parent_uuid == uuid;
                          }) ||
      std::ranges::any_of(description_.prefab_instances, [&uuid](const auto& instance) {
        return instance.parent_uuid && *instance.parent_uuid == uuid;
      })) {
    return gneiss::engine::result::invalid_state;
  }
  auto result = native_world_.entity_destroy(gneiss::entity_id{found->entity});
  if (result != gneiss::engine::result::success) {
    return result;
  }
  result = native_world_.node_destroy(gneiss::scene_node_id{found->node});
  if (result != gneiss::engine::result::success &&
      result != gneiss::engine::result::invalid_handle) {
    return result;
  }
  objects_.erase(found);
  description_.objects.erase(description_.objects.begin() + static_cast<std::ptrdiff_t>(index));
  loader_.release_unused();
  return gneiss::engine::result::success;
}

gneiss::engine::result scene_instance::serialize(std::string& out_json) const {
  auto current = description_;
  gneiss_entity_id active_camera = GNEISS_NULL_ENTITY_ID;
  const auto active_result =
      gneiss::engine::write_identifier<entity_id>(active_camera, [&](auto& native_output) {
        return native_world_.get_active_camera(native_output);
      });
  if (active_result != gneiss::engine::result::success &&
      active_result != gneiss::engine::result::not_ready) {
    return active_result;
  }
  for (std::size_t index = 0; index < objects_.size(); ++index) {
    const auto& runtime = objects_[index];
    auto& author = current.objects[index];
    gneiss::engine::transform transform = {};
    auto result =
        native_world_.entity_get_local_transform(gneiss::entity_id{runtime.entity}, transform);
    if (result != gneiss::engine::result::success) {
      return result;
    }
    std::ranges::copy(transform.translation, author.translation.begin());
    std::ranges::copy(transform.rotation, author.rotation.begin());
    std::ranges::copy(transform.scale, author.scale.begin());
    if (author.camera) {
      gneiss::engine::camera_settings camera;
      result = native_world_.entity_get_camera(gneiss::entity_id{runtime.entity}, camera);
      if (result != gneiss::engine::result::success) {
        return result;
      }
      author.camera->vertical_field_of_view_radians = camera.vertical_field_of_view_radians;
      author.camera->near_plane = camera.near_plane;
      author.camera->far_plane = camera.far_plane;
      author.camera->is_primary = runtime.entity == active_camera;
    }
  }
  if (prefab_instances_.size() != current.prefab_instances.size()) {
    return gneiss::engine::result::invalid_state;
  }
  for (std::size_t index = 0; index < prefab_instances_.size(); ++index) {
    auto& author = current.prefab_instances[index];
    gneiss::engine::transform transform = {};
    const auto result = native_world_.node_get_local_transform(
        gneiss::scene_node_id{prefab_instances_[index]->root()}, transform);
    if (result != gneiss::engine::result::success) {
      return result;
    }
    std::ranges::copy(transform.translation, author.translation.begin());
    std::ranges::copy(transform.rotation, author.rotation.begin());
    std::ranges::copy(transform.scale, author.scale.begin());
  }
  return gneiss::engine::serialize_scene_description(current, out_json);
}

gneiss::engine::result scene_instances::capture_render_assets(
    std::vector<gneiss::engine::render_asset_ref>& output) noexcept {
  try {
    const auto owner = native_world_.replace_mesh_renderers({});
    if (owner != gneiss::engine::result::success) {
      return owner;
    }
    std::vector<gneiss::engine::render_asset_ref> captured;
    std::unordered_set<std::uint64_t> seen;
    instances_.for_each(gneiss::engine::resource_type::scene_instance,
                        [&](const auto& instance) { instance->append_render_assets(captured); });
    std::erase_if(captured, [&](const auto& lease) {
      return !seen.insert(gneiss::engine::asset_rid(lease)).second;
    });
    output = std::move(captured);
    return gneiss::engine::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

scene_instances::scene_instances(gneiss::engine::world& native_world, gneiss_world world,
                                 const gneiss::engine::virtual_file_system& file_system,
                                 gneiss::engine::render_asset_loader& loader,
                                 gneiss::engine::prefab_asset_loader& prefab_loader) noexcept
    : native_world_(native_world), reflection_context_(world), file_system_(file_system),
      loader_(loader), prefab_loader_(prefab_loader), domain_(allocate_domain()),
      instances_(domain_) {
  (void)gneiss::engine::create_property_registry(registry_);
}

scene_instances::~scene_instances() noexcept = default;

gneiss::engine::result scene_instances::load(std::string_view uri,
                                             gneiss_scene_instance* out_instance) noexcept {
  if (out_instance == nullptr || !is_valid()) {
    return gneiss::engine::result::invalid_argument;
  }
  *out_instance = GNEISS_NULL_SCENE_INSTANCE;
  try {
    gneiss::engine::scene_description description;
    gneiss::engine::scene_diagnostic diagnostic;
    auto result =
        gneiss::engine::load_scene_description(file_system_, uri, description, diagnostic);
    if (result != gneiss::engine::result::success) {
      return result;
    }
    auto instance = std::make_unique<scene_instance>(native_world_, reflection_context_, loader_,
                                                     prefab_loader_, registry_);
    result = instance->initialize(std::move(description));
    if (result != gneiss::engine::result::success) {
      return result;
    }
    return instances_.create(gneiss::engine::resource_type::scene_instance, std::move(instance),
                             out_instance);
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

gneiss::engine::result scene_instances::reload(gneiss_scene_instance instance,
                                               std::string_view uri) noexcept {
  try {
    auto* value = instances_.get(instance, gneiss::engine::resource_type::scene_instance);
    if (value == nullptr || *value == nullptr) {
      return gneiss::engine::result::invalid_handle;
    }
    gneiss::engine::scene_description candidate;
    gneiss::engine::scene_diagnostic diagnostic;
    const auto result =
        gneiss::engine::load_scene_description(file_system_, uri, candidate, diagnostic);
    return result == gneiss::engine::result::success ? (*value)->reload(std::move(candidate))
                                                     : result;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

gneiss::engine::result scene_instances::reload_prefab(gneiss_scene_instance instance,
                                                      std::string_view uri) noexcept {
  try {
    auto* value = instances_.get(instance, gneiss::engine::resource_type::scene_instance);
    if (value == nullptr || *value == nullptr) {
      return gneiss::engine::result::invalid_handle;
    }
    return (*value)->reload_prefab(uri);
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

gneiss::engine::result scene_instances::create_empty(std::string_view scene_uuid,
                                                     gneiss_scene_instance* out_instance) noexcept {
  if (out_instance == nullptr || !is_valid()) {
    return gneiss::engine::result::invalid_argument;
  }
  *out_instance = GNEISS_NULL_SCENE_INSTANCE;
  try {
    const std::string json = std::string{"{\"format\":\"gneiss.scene\",\"version\":4,"} +
                             "\"scene_uuid\":\"" + std::string{scene_uuid} +
                             "\",\"objects\":[],\"prefab_instances\":[]}";
    gneiss::engine::scene_description description;
    gneiss::engine::scene_diagnostic diagnostic;
    const auto result = gneiss::engine::parse_scene_description(json, description, diagnostic);
    if (result != gneiss::engine::result::success) {
      return result;
    }
    auto instance = std::make_unique<scene_instance>(native_world_, reflection_context_, loader_,
                                                     prefab_loader_, registry_);
    instance->initialize_staged(std::move(description));
    return instances_.create(gneiss::engine::resource_type::scene_instance, std::move(instance),
                             out_instance);
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

gneiss::engine::result scene_instances::serialize(gneiss_scene_instance instance,
                                                  std::string& out_json) const noexcept {
  try {
    const auto* value = instances_.get(instance, gneiss::engine::resource_type::scene_instance);
    if (value == nullptr || *value == nullptr) {
      return gneiss::engine::result::invalid_handle;
    }
    return (*value)->serialize(out_json);
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

gneiss::engine::result scene_instances::unload(gneiss_scene_instance instance) noexcept {
  return instances_.destroy(instance, gneiss::engine::resource_type::scene_instance);
}

gneiss::engine::result scene_instances::find_node(gneiss_scene_instance instance,
                                                  std::string_view uuid,
                                                  scene_node_id& out_node_value) const noexcept {
  auto out_node_storage = out_node_value.get();
  auto* out_node = &out_node_storage;
  const auto operation_result = [&]() -> gneiss::engine::result {
    if (out_node == nullptr) {
      return gneiss::engine::result::invalid_argument;
    }
    *out_node = GNEISS_NULL_SCENE_NODE_ID;
    const auto* value = instances_.get(instance, gneiss::engine::resource_type::scene_instance);
    if (value == nullptr || *value == nullptr) {
      return gneiss::engine::result::invalid_handle;
    }
    *out_node = (*value)->find_node(uuid);
    return *out_node == GNEISS_NULL_SCENE_NODE_ID ? gneiss::engine::result::not_found
                                                  : gneiss::engine::result::success;
  }();
  out_node_value = scene_node_id{out_node_storage};
  return operation_result;
}

gneiss::engine::result scene_instances::get_node_count(gneiss_scene_instance instance,
                                                       std::uint64_t* out_count) const noexcept {
  if (out_count == nullptr) {
    return gneiss::engine::result::invalid_argument;
  }
  *out_count = 0U;
  const auto* value = instances_.get(instance, gneiss::engine::resource_type::scene_instance);
  if (value == nullptr || *value == nullptr) {
    return gneiss::engine::result::invalid_handle;
  }
  *out_count = (*value)->node_count();
  return gneiss::engine::result::success;
}

gneiss::engine::result
scene_instances::get_prefab_node_count(gneiss_scene_instance instance,
                                       std::uint64_t* out_count) const noexcept {
  if (out_count == nullptr) {
    return gneiss::engine::result::invalid_argument;
  }
  *out_count = 0U;
  const auto* value = instances_.get(instance, gneiss::engine::resource_type::scene_instance);
  if (value == nullptr || *value == nullptr) {
    return gneiss::engine::result::invalid_handle;
  }
  *out_count = (*value)->get_prefab_node_count();
  return gneiss::engine::result::success;
}

gneiss::engine::result
scene_instances::get_prefab_node_info(gneiss_scene_instance instance, std::uint64_t index,
                                      prefab_node_view& out_info) const noexcept {
  out_info = {};
  const auto* value = instances_.get(instance, gneiss::engine::resource_type::scene_instance);
  return value == nullptr || *value == nullptr ? gneiss::engine::result::invalid_handle
                                               : (*value)->get_prefab_node_info(index, out_info);
}

gneiss::engine::result
scene_instances::create_prefab_instance(gneiss_scene_instance instance, const prefab_creation& desc,
                                        scene_node_id& out_root_value) noexcept {
  auto out_root_storage = out_root_value.get();
  auto* out_root = &out_root_storage;
  const auto operation_result = [&]() -> gneiss::engine::result {
    if (out_root == nullptr) {
      return gneiss::engine::result::invalid_argument;
    }
    *out_root = GNEISS_NULL_SCENE_NODE_ID;
    try {
      auto* value = instances_.get(instance, gneiss::engine::resource_type::scene_instance);
      return value == nullptr || *value == nullptr
                 ? gneiss::engine::result::invalid_handle
                 : (*value)->create_prefab_instance(desc, out_root);
    } catch (const std::bad_alloc&) {
      return gneiss::engine::result::out_of_memory;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }();
  out_root_value = scene_node_id{out_root_storage};
  return operation_result;
}

gneiss::engine::result scene_instances::set_prefab_instance_name(gneiss_scene_instance instance,
                                                                 scene_node_id root_value,
                                                                 std::string_view name) noexcept {
  auto root = root_value.get();
  try {
    auto* value = instances_.get(instance, gneiss::engine::resource_type::scene_instance);
    return value == nullptr || *value == nullptr ? gneiss::engine::result::invalid_handle
                                                 : (*value)->set_prefab_instance_name(root, name);
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

gneiss::engine::result
scene_instances::set_prefab_source_transform(gneiss_scene_instance instance,
                                             scene_node_id node_value,
                                             const gneiss::engine::transform& transform) noexcept {
  auto node = node_value.get();
  try {
    auto* value = instances_.get(instance, gneiss::engine::resource_type::scene_instance);
    return value == nullptr || *value == nullptr
               ? gneiss::engine::result::invalid_handle
               : (*value)->set_prefab_source_transform(node, transform);
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

gneiss::engine::result scene_instances::destroy_prefab_instance(gneiss_scene_instance instance,
                                                                scene_node_id root_value) noexcept {
  auto root = root_value.get();
  auto* value = instances_.get(instance, gneiss::engine::resource_type::scene_instance);
  return value == nullptr || *value == nullptr ? gneiss::engine::result::invalid_handle
                                               : (*value)->destroy_prefab_instance(root);
}

gneiss::engine::result
scene_instances::refresh_prefab_instance(gneiss_scene_instance instance, scene_node_id root_value,
                                         scene_node_id& out_new_root_value,
                                         gneiss_scene_prefab_refresh_token* out_token) noexcept {
  auto root = root_value.get();
  auto out_new_root_storage = out_new_root_value.get();
  auto* out_new_root = &out_new_root_storage;
  const auto operation_result = [&]() -> gneiss::engine::result {
    if (out_new_root == nullptr || out_token == nullptr) {
      return gneiss::engine::result::invalid_argument;
    }
    *out_new_root = GNEISS_NULL_SCENE_NODE_ID;
    *out_token = GNEISS_NULL_SCENE_PREFAB_REFRESH_TOKEN;
    try {
      auto* value = instances_.get(instance, gneiss::engine::resource_type::scene_instance);
      return value == nullptr || *value == nullptr
                 ? gneiss::engine::result::invalid_handle
                 : (*value)->refresh_prefab_instance(root, out_new_root, out_token);
    } catch (const std::bad_alloc&) {
      return gneiss::engine::result::out_of_memory;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }();
  out_new_root_value = scene_node_id{out_new_root_storage};
  return operation_result;
}

gneiss::engine::result
scene_instances::toggle_prefab_refresh(gneiss_scene_instance instance,
                                       gneiss_scene_prefab_refresh_token token,
                                       scene_node_id& out_new_root_value) noexcept {
  auto out_new_root_storage = out_new_root_value.get();
  auto* out_new_root = &out_new_root_storage;
  const auto operation_result = [&]() -> gneiss::engine::result {
    if (out_new_root == nullptr || token == GNEISS_NULL_SCENE_PREFAB_REFRESH_TOKEN) {
      return gneiss::engine::result::invalid_argument;
    }
    *out_new_root = GNEISS_NULL_SCENE_NODE_ID;
    try {
      auto* value = instances_.get(instance, gneiss::engine::resource_type::scene_instance);
      return value == nullptr || *value == nullptr
                 ? gneiss::engine::result::invalid_handle
                 : (*value)->toggle_prefab_refresh(token, out_new_root);
    } catch (const std::bad_alloc&) {
      return gneiss::engine::result::out_of_memory;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }();
  out_new_root_value = scene_node_id{out_new_root_storage};
  return operation_result;
}

gneiss::engine::result
scene_instances::release_prefab_refresh(gneiss_scene_instance instance,
                                        gneiss_scene_prefab_refresh_token token) noexcept {
  if (token == GNEISS_NULL_SCENE_PREFAB_REFRESH_TOKEN) {
    return gneiss::engine::result::invalid_argument;
  }
  auto* value = instances_.get(instance, gneiss::engine::resource_type::scene_instance);
  return value == nullptr || *value == nullptr ? gneiss::engine::result::invalid_handle
                                               : (*value)->release_prefab_refresh(token);
}

gneiss::engine::result scene_instances::get_node_info(gneiss_scene_instance instance,
                                                      std::uint64_t index,
                                                      scene_node_view& out_info,
                                                      node_query_detail detail) const noexcept {
  out_info = {};
  const auto* value = instances_.get(instance, gneiss::engine::resource_type::scene_instance);
  if (value == nullptr || *value == nullptr) {
    return gneiss::engine::result::invalid_handle;
  }
  return (*value)->get_node_info(index, out_info, detail);
}

gneiss::engine::result scene_instances::create_node(gneiss_scene_instance instance,
                                                    const node_creation& desc,
                                                    scene_node_id& out_node_value) noexcept {
  auto out_node_storage = out_node_value.get();
  auto* out_node = &out_node_storage;
  const auto operation_result = [&]() -> gneiss::engine::result {
    if (out_node == nullptr) {
      return gneiss::engine::result::invalid_argument;
    }
    *out_node = GNEISS_NULL_SCENE_NODE_ID;
    try {
      auto* value = instances_.get(instance, gneiss::engine::resource_type::scene_instance);
      return value == nullptr || *value == nullptr ? gneiss::engine::result::invalid_handle
                                                   : (*value)->create_node(desc, out_node);
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }();
  out_node_value = scene_node_id{out_node_storage};
  return operation_result;
}

gneiss::engine::result scene_instances::set_node_name(gneiss_scene_instance instance,
                                                      scene_node_id node_value,
                                                      std::string_view name) noexcept {
  auto node = node_value.get();
  try {
    auto* value = instances_.get(instance, gneiss::engine::resource_type::scene_instance);
    return value == nullptr || *value == nullptr ? gneiss::engine::result::invalid_handle
                                                 : (*value)->set_node_name(node, name);
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

gneiss::engine::result scene_instances::reparent_node(gneiss_scene_instance instance,
                                                      scene_node_id node_value,
                                                      scene_node_id parent_value) noexcept {
  auto node = node_value.get();
  auto parent = parent_value.get();
  try {
    auto* value = instances_.get(instance, gneiss::engine::resource_type::scene_instance);
    return value == nullptr || *value == nullptr ? gneiss::engine::result::invalid_handle
                                                 : (*value)->reparent_node(node, parent);
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

gneiss::engine::result scene_instances::capture_subtree(gneiss_scene_instance instance,
                                                        scene_node_id root_value,
                                                        std::string& out_snapshot) const noexcept {
  auto root = root_value.get();
  try {
    const auto* value = instances_.get(instance, gneiss::engine::resource_type::scene_instance);
    return value == nullptr || *value == nullptr ? gneiss::engine::result::invalid_handle
                                                 : (*value)->capture_subtree(root, out_snapshot);
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

gneiss::engine::result scene_instances::restore_subtree(gneiss_scene_instance instance,
                                                        std::string_view snapshot,
                                                        scene_node_id parent_value,
                                                        std::span<const uuid_mapping> mappings,
                                                        scene_node_id& out_root_value) noexcept {
  auto parent = parent_value.get();
  auto out_root_storage = out_root_value.get();
  auto* out_root = &out_root_storage;
  const auto operation_result = [&]() -> gneiss::engine::result {
    if (out_root == nullptr) {
      return gneiss::engine::result::invalid_argument;
    }
    *out_root = GNEISS_NULL_SCENE_NODE_ID;
    try {
      auto* value = instances_.get(instance, gneiss::engine::resource_type::scene_instance);
      return value == nullptr || *value == nullptr
                 ? gneiss::engine::result::invalid_handle
                 : (*value)->restore_subtree(snapshot, parent, mappings, out_root);
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }();
  out_root_value = scene_node_id{out_root_storage};
  return operation_result;
}

gneiss::engine::result scene_instances::destroy_subtree(gneiss_scene_instance instance,
                                                        scene_node_id root_value) noexcept {
  auto root = root_value.get();
  try {
    auto* value = instances_.get(instance, gneiss::engine::resource_type::scene_instance);
    return value == nullptr || *value == nullptr ? gneiss::engine::result::invalid_handle
                                                 : (*value)->destroy_subtree(root);
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

gneiss::engine::result
scene_instances::create_mesh_renderer_node(gneiss_scene_instance instance,
                                           const mesh_renderer_node_creation& desc,
                                           scene_node_id& out_node_value) noexcept {
  auto out_node_storage = out_node_value.get();
  auto* out_node = &out_node_storage;
  const auto operation_result = [&]() -> gneiss::engine::result {
    if (out_node == nullptr) {
      return gneiss::engine::result::invalid_argument;
    }
    *out_node = GNEISS_NULL_SCENE_NODE_ID;
    try {
      auto* value = instances_.get(instance, gneiss::engine::resource_type::scene_instance);
      return value == nullptr || *value == nullptr
                 ? gneiss::engine::result::invalid_handle
                 : (*value)->create_mesh_renderer_node(desc, out_node);
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }();
  out_node_value = scene_node_id{out_node_storage};
  return operation_result;
}

gneiss::engine::result scene_instances::create_mesh_renderer_node_ready(
    gneiss_scene_instance instance, const mesh_renderer_node_creation& desc,
    const mesh_binding& prepared, scene_node_id& output_value) noexcept try {
  auto output = output_value.get();
  const auto operation_result = [&]() -> gneiss::engine::result {
    output = GNEISS_NULL_SCENE_NODE_ID;
    const auto owner = native_world_.replace_mesh_renderers({});
    if (owner != gneiss::engine::result::success) {
      return owner;
    }
    auto* value = instances_.get(instance, gneiss::engine::resource_type::scene_instance);
    return value == nullptr || *value == nullptr
               ? gneiss::engine::result::invalid_handle
               : (*value)->create_mesh_renderer_node(desc, &output, &prepared);
  }();
  output_value = scene_node_id{output};
  return operation_result;
} catch (const std::bad_alloc&) {
  return gneiss::engine::result::out_of_memory;
} catch (...) {
  return gneiss::engine::result::internal;
}

gneiss::engine::result scene_instances::capture_mesh_binding(gneiss_scene_instance instance,
                                                             scene_node_id node,
                                                             mesh_binding& output) const noexcept {
  output = {};
  const auto owner = native_world_.replace_mesh_renderers({});
  if (owner != gneiss::engine::result::success) {
    return owner;
  }
  const auto* value = instances_.get(instance, gneiss::engine::resource_type::scene_instance);
  return value == nullptr || *value == nullptr ? gneiss::engine::result::invalid_handle
                                               : (*value)->capture_mesh_binding(node, output);
}

gneiss::engine::result
scene_instances::replace_mesh_binding(gneiss_scene_instance instance, scene_node_id node,
                                      const mesh_binding& expected,
                                      const mesh_binding& replacement) noexcept {
  const auto owner = native_world_.replace_mesh_renderers({});
  if (owner != gneiss::engine::result::success) {
    return owner;
  }
  using type = gneiss::engine::render_asset_type;
  if (gneiss::engine::asset_kind(replacement.mesh) != type::mesh ||
      gneiss::engine::asset_kind(replacement.material) != type::material ||
      !loader_.owns(replacement.mesh) || !loader_.owns(replacement.material)) {
    return gneiss::engine::result::invalid_argument;
  }
  auto* value = instances_.get(instance, gneiss::engine::resource_type::scene_instance);
  return value == nullptr || *value == nullptr
             ? gneiss::engine::result::invalid_handle
             : (*value)->replace_mesh_binding(node, expected, replacement);
}

gneiss::engine::result scene_instances::set_mesh_renderer(gneiss_scene_instance instance,
                                                          scene_node_id node_value,
                                                          std::string_view mesh_uri,
                                                          std::string_view material_uri) noexcept {
  auto node = node_value.get();
  try {
    auto* value = instances_.get(instance, gneiss::engine::resource_type::scene_instance);
    return value == nullptr || *value == nullptr
               ? gneiss::engine::result::invalid_handle
               : (*value)->set_mesh_renderer(node, mesh_uri, material_uri);
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

gneiss::engine::result
scene_instances::validate_editable_node(gneiss_scene_instance instance,
                                        scene_node_id node_value) const noexcept {
  auto node = node_value.get();
  const auto* value = instances_.get(instance, gneiss::engine::resource_type::scene_instance);
  if (value == nullptr || *value == nullptr) {
    return gneiss::engine::result::invalid_handle;
  }
  return (*value)->contains_editable_node(scene_node_id{node})
             ? gneiss::engine::result::success
             : gneiss::engine::result::invalid_handle;
}

gneiss::engine::result
scene_instances::set_camera(gneiss_scene_instance instance, scene_node_id node_value,
                            const gneiss::engine::camera_description& desc) noexcept {
  auto node = node_value.get();
  auto* value = instances_.get(instance, gneiss::engine::resource_type::scene_instance);
  return value == nullptr || *value == nullptr ? gneiss::engine::result::invalid_handle
                                               : (*value)->set_camera(node, desc);
}

gneiss::engine::result scene_instances::remove_camera(gneiss_scene_instance instance,
                                                      scene_node_id node_value) noexcept {
  auto node = node_value.get();
  auto* value = instances_.get(instance, gneiss::engine::resource_type::scene_instance);
  return value == nullptr || *value == nullptr ? gneiss::engine::result::invalid_handle
                                               : (*value)->remove_camera(node);
}

gneiss::engine::result scene_instances::remove_mesh_renderer(gneiss_scene_instance instance,
                                                             scene_node_id node_value) noexcept {
  auto node = node_value.get();
  auto* value = instances_.get(instance, gneiss::engine::resource_type::scene_instance);
  return value == nullptr || *value == nullptr ? gneiss::engine::result::invalid_handle
                                               : (*value)->remove_mesh_renderer(node);
}

gneiss::engine::result scene_instances::destroy_node(gneiss_scene_instance instance,
                                                     scene_node_id node_value) noexcept {
  auto node = node_value.get();
  try {
    auto* value = instances_.get(instance, gneiss::engine::resource_type::scene_instance);
    return value == nullptr || *value == nullptr ? gneiss::engine::result::invalid_handle
                                                 : (*value)->destroy_node(node);
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

} // namespace gneiss::engine
