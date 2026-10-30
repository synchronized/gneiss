// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/scene/prefab_binding.hpp"
#include "engine/core/error.hpp"
#include "engine/core/scene_types.hpp"
#include "engine/function/scene/prefab_property_override.hpp"
#include "engine/resource/scene/object_hierarchy.hpp"

#include "engine/core/identifier_conversion.hpp"
#include "engine/core/meta/registration.hpp"
#include "engine/function/world/component_identity.hpp"

#include "engine/function/world/world.hpp"
#include "engine/function/world/world_reflection.hpp"

#include "engine/function/scene/scene_object.hpp"
#include "engine/function/scene/structural_diff.hpp"

#include <algorithm>
#include <new>
#include <type_traits>
#include <unordered_map>

namespace gneiss::engine {

prefab_binding::prefab_binding(world& native_world, resource_service& loader,
                               prefab_asset_ref prefab, gneiss::engine::guid instance_uuid) noexcept
    : native_world_(native_world), loader_(loader), prefab_(std::move(prefab)),
      instance_uuid_(std::move(instance_uuid)) {}

engine::error prefab_binding::validate_reload(const prefab_asset_ref& candidate,
                                              const prefab_reference_description& reference,
                                              scene_diagnostic* diagnostic) {
  if (!candidate || !prefab_ || candidate.get()->uuid != prefab_.get()->uuid ||
      candidate.get()->objects.empty()) {
    return engine::error::invalid_argument;
  }
  auto hierarchy = inspect_object_hierarchy(candidate->objects, candidate->root_uuid);
  if (!hierarchy)
    return hierarchy.error().error;
  if (candidate->root_uuid != prefab_->root_uuid || find_node(candidate->root_uuid) != root_node_)
    return error::unsupported;
  structural_diff diff;
  auto result = build_structural_diff(prefab_.get()->objects, candidate.get()->objects, diff);
  if (result != engine::error::success) {
    return result;
  }

  prefab_description effective;
  result = prepare_prefab_reference(*candidate, reference, effective, diagnostic);
  if (result.failed())
    return result;
  for (const auto& object : effective.objects) {
    auto dependencies = component_dependencies(object.components);
    if (!dependencies)
      return dependencies.error();
    for (const auto& source : *dependencies) {
      auto ready = loader_.find(source);
      if (!ready)
        return ready.error();
    }
  }
  return engine::error::success;
}

engine::error prefab_binding::reload(prefab_asset_ref candidate,
                                     const prefab_reference_description& reference) {

  auto result = validate_reload(candidate, reference);
  if (result != engine::error::success) {
    return result;
  }
  prefab_description effective;
  result = prepare_prefab_reference(*candidate, reference, effective);
  if (result.failed())
    return result;
  auto current = *prefab_;
  for (auto& object : current.objects) {
    auto captured = capture_components(native_world_, find_node(object.uuid));
    if (!captured)
      return captured.error();
    object.components = std::move(*captured);
    result = native_world_.node_get_local_transform(find_node(object.uuid), object.transform);
    if (result.failed())
      return result;
  }
  structural_diff diff;
  result = build_structural_diff(current.objects, effective.objects, diff);
  if (result.failed())
    return result;

  struct staged_node {
    prefab_author_address address;
    std::string name;
    std::uint64_t entity{};
    scene_node_id node{};
    engine::mesh mesh;
    engine::material material;
  };
  std::vector<staged_node> staged;
  staged.reserve(effective.objects.size());
  std::unordered_map<guid, std::size_t, guid_hash> old_indices;
  old_indices.reserve(nodes_.size());
  for (std::size_t index = 0U; index < nodes_.size(); ++index) {
    old_indices.emplace(nodes_[index].address.source_node_uuid, index);
  }
  for (const auto& source : effective.objects) {
    staged_node target;
    target.address = {instance_uuid_, source.uuid};
    target.name = source.name;
    if (const auto previous = old_indices.find(source.uuid); previous != old_indices.end()) {
      target.entity = scene_object_entity(native_world_, nodes_[previous->second].node);
      target.node = nodes_[previous->second].node;
    }
    auto renderer = resolve_component_renderer(source.components, loader_);
    if (!renderer)
      return renderer.error();
    target.mesh = renderer->mesh;
    target.material = renderer->material;
    staged.push_back(std::move(target));
  }

  std::vector<runtime_node> members;
  members.reserve(staged.size());
  std::vector<scene_node_identity> identities;
  identities.reserve(staged.size());
  for (const auto& value : staged) {
    members.push_back({value.address, value.node, true});
    identities.push_back({value.address.source_node_uuid, value.name});
  }
  std::unordered_map<guid, scene_node_id, guid_hash> runtime_nodes;
  runtime_nodes.reserve(staged.size());
  for (const auto& target : staged) {
    if (target.node != null_scene_node_id) {
      runtime_nodes.emplace(target.address.source_node_uuid, target.node);
    }
  }
  std::vector<std::size_t> additions;
  additions.reserve(diff.added.size());
  std::uint64_t active_camera_before = UINT64_C(0);
  (void)engine::write_identifier<entity_id>(active_camera_before, [&](auto& native_output) {
    return native_world_.get_active_camera(native_output);
  });
  const auto rollback_additions = [&]() noexcept {
    for (auto iterator = additions.rbegin(); iterator != additions.rend(); ++iterator) {
      auto& target = staged[*iterator];
      if (target.node) {
        (void)native_world_.object_destroy(target.node);
      }
    }
    if (active_camera_before != UINT64_C(0)) {
      (void)native_world_.set_active_camera(gneiss::entity_id{active_camera_before});
    }
  };
  for (const auto& addition : diff.added) {
    const auto& source = effective.objects[addition.new_index];
    auto& target = staged[addition.new_index];
    const auto parent = source.parent_uuid ? runtime_nodes.at(*source.parent_uuid) : root_node_;
    auto created = create_scene_object(native_world_, source, parent,
                                       {target.mesh, target.material}, &loader_);
    result = created ? error::success : created.error();
    if (created) {
      target.node = *created;
      target.entity = scene_object_entity(native_world_, target.node);
    }

    if (result != engine::error::success) {
      if (target.node) {
        (void)native_world_.object_destroy(target.node);
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
    scene_node_id parent = null_scene_node_id;
    math::transform transform = {};
    component_descriptions components;
    mesh_renderer renderer;
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
      if (has_change(snapshot.changes, structural_change::components))
        (void)replace_components({native_world_, target.node, snapshot.renderer, &loader_},
                                 snapshot.components);
    }
  };

  for (const auto& update : diff.updated) {
    auto& target = staged[update.new_index];
    update_snapshot snapshot{};
    snapshot.old_index = update.old_index;
    snapshot.new_index = update.new_index;
    snapshot.changes = update.changes;
    result = native_world_.node_get_parent(gneiss::scene_node_id{target.node}, snapshot.parent);
    if (result == engine::error::success) {
      result = native_world_.node_get_local_transform(gneiss::scene_node_id{target.node},
                                                      snapshot.transform);
    }
    if (result == engine::error::success &&
        has_change(update.changes, structural_change::components)) {
      auto captured = capture_components(native_world_, target.node);
      if (captured)
        snapshot.components = std::move(*captured);
      else
        result = captured.error();
    }
    if (result != engine::error::success) {
      rollback_updates();
      rollback_additions();
      return result;
    }
    auto old_renderer = native_world_.node_mesh_renderer(target.node);
    if (old_renderer)
      snapshot.renderer = *old_renderer;
    else if (old_renderer.error() != error::not_found) {
      rollback_updates();
      rollback_additions();
      return old_renderer.error();
    }
    applied.push_back(std::move(snapshot));
    const auto& source = effective.objects[update.new_index];
    if (target.node != root_node_ && has_change(update.changes, structural_change::parent)) {
      const auto parent = source.parent_uuid ? runtime_nodes.at(*source.parent_uuid) : root_node_;
      result = native_world_.node_reparent(gneiss::scene_node_id{target.node},
                                           gneiss::scene_node_id{parent});
    }
    if (result == engine::error::success && target.node != root_node_ &&
        has_change(update.changes, structural_change::transform)) {
      result = native_world_.node_set_local_transform(gneiss::scene_node_id{target.node},
                                                      source.transform);
    }
    if (result == engine::error::success &&
        has_change(update.changes, structural_change::components))
      result =
          replace_components({native_world_, target.node, {target.mesh, target.material}, &loader_},
                             source.components);
    if (result != engine::error::success) {
      rollback_updates();
      rollback_additions();
      return result;
    }
  }

  for (std::size_t index = 0; index < staged.size(); ++index)
    members[index].node = staged[index].node;
  auto previous_nodes = std::move(nodes_);
  nodes_ = std::move(members);
  for (const auto& removal : diff.removed) {
    const auto& target = previous_nodes[removal.old_index];
    (void)native_world_.object_destroy(target.node);
  }
  for (std::size_t index = 0; index < nodes_.size(); ++index) {
    if (nodes_[index].node == root_node_)
      continue;
    result = native_world_.node_set_identity(nodes_[index].node, std::move(identities[index]));
    if (result.failed())
      return result;
  }
  prefab_ = std::move(candidate);
  applied.clear();
  staged.clear();
  loader_.collect_unused();
  return engine::error::success;
}

engine::error
prefab_binding::apply_overrides(std::span<const prefab_property_override> overrides) noexcept try {
  for (const auto& override_value : overrides) {
    const auto validation = validate_prefab_property_override(override_value);
    if (validation != engine::error::success) {
      return validation;
    }
    const auto found =
        std::ranges::find(nodes_, override_value.key.source_node_uuid,
                          [](const runtime_node& node) { return node.address.source_node_uuid; });
    if (found == nodes_.end()) {
      return engine::error::not_found;
    }
    if (found->node == root_node_ && override_value.key.type_guid == transform_component_type)
      return error::invalid_argument;
    if (!override_value.key.path.empty()) {
      auto captured = capture_components(native_world_, found->node);
      if (!captured)
        return captured.error();
      const auto* source = captured->find(override_value.key.type_guid);
      if (!source)
        return error::not_found;
      auto changed = modify_component(*source, [&](meta::mutable_instance root) -> error {
        auto field = root.property(override_value.key.property_id);
        if (!field)
          return field.error();
        auto target = meta::resolve_path(*field, override_value.key.path);
        return target ? target->write_value(override_value.value) : target.error();
      });
      if (!changed)
        return changed.error();
      auto renderer = native_world_.node_mesh_renderer(found->node);
      if (!renderer && renderer.error() != error::not_found)
        return renderer.error();
      auto status = install_components(
          {native_world_, found->node, renderer ? *renderer : mesh_renderer{}, &loader_},
          {*changed});
      if (status.failed())
        return status;
      continue;
    }
    const auto result =
        override_value.key.type_guid == transform_component_type
            ? engine::write_component_property(native_world_,
                                               scene_object_entity(native_world_, found->node),
                                               override_value.key.type_guid,
                                               override_value.key.property_id, override_value.value)
            : edit_runtime_component({native_world_, found->node, {}, &loader_},
                                     override_value.key.type_guid, override_value.key.property_id,
                                     override_value.value);
    if (result != engine::error::success) {
      return result;
    }
  }
  return engine::error::success;
} catch (...) {
  return engine::error::internal;
}

engine::error prefab_binding::create_root(scene_node_id parent, const math::transform& transform) {
  if (!prefab_ || root_node_ || !nodes_.empty())
    return error::invalid_state;
  const auto& objects = prefab_->objects;
  auto hierarchy = inspect_object_hierarchy(objects, prefab_->root_uuid);
  if (!hierarchy)
    return hierarchy.error().error;
  for (const auto& object : objects) {
    const auto status = validate_scene_components(object);
    if (status.failed())
      return status;
  }
  const auto root_index = hierarchy->indices.at(prefab_->root_uuid);
  const auto& root = objects[root_index];
  nodes_.resize(objects.size());
  auto& member = nodes_[root_index];
  member.address = {instance_uuid_, root.uuid};
  auto created = native_world_.object_create(parent, {instance_uuid_, {}}, transform);
  if (!created)
    return created.error();
  root_node_ = *created;
  member.node = root_node_;
  return error::success;
}
engine::error prefab_binding::create_node(std::size_t index, const object_description& source,
                                          scene_node_id parent, engine::mesh mesh,
                                          engine::material material) {
  if (!root_node_ || !prefab_ || index >= nodes_.size() || nodes_[index].creation_attempted)
    return error::invalid_state;
  if (source.uuid != prefab_->objects[index].uuid)
    return error::invalid_argument;
  auto& target = nodes_[index];
  target.address = {instance_uuid_, source.uuid};

  target.creation_attempted = true;
  if (source.uuid == prefab_->root_uuid) {
    if (target.node != root_node_)
      return error::invalid_state;
    return configure_scene_object(native_world_, root_node_, source, {mesh, material}, &loader_);
  }
  auto created = create_scene_object(native_world_, source, parent, {mesh, material}, &loader_);
  if (!created)
    return created.error();
  target.node = *created;
  return error::success;
}

engine::error prefab_binding::apply_override(const prefab_property_override& value) noexcept {
  return apply_overrides(std::span(&value, 1U));
}

scene_node_id prefab_binding::find_node(gneiss::engine::guid source_node_uuid) const noexcept {
  const auto found = std::ranges::find(nodes_, source_node_uuid, [](const runtime_node& node) {
    return node.address.source_node_uuid;
  });
  return found == nodes_.end() ? null_scene_node_id : found->node;
}

const object_description*
prefab_binding::find_source_object(gneiss::engine::guid source_node_uuid) const noexcept {
  const auto& objects = prefab_.get()->objects;
  const auto found = std::ranges::find(objects, source_node_uuid, &object_description::uuid);
  return found == objects.end() ? nullptr : &*found;
}

engine::error prefab_binding::get_node_info(std::size_t index, node_info& out_info) const noexcept {
  out_info = {};
  if (index >= nodes_.size()) {
    return engine::error::not_found;
  }
  const auto& source = nodes_[index];
  scene_node_id parent{};
  const auto result = native_world_.node_get_parent(gneiss::scene_node_id{source.node}, parent);
  if (result != engine::error::success) {
    return result;
  }
  out_info = {.address = &source.address,
              .name = scene_object_identity(native_world_, source.node).name,
              .entity = scene_object_entity(native_world_, source.node),
              .node = source.node,
              .parent = parent};
  return engine::error::success;
}

} // namespace gneiss::engine
