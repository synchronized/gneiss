// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/scene/scene_instance.hpp"
#include "engine/core/error.hpp"
#include "engine/function/resource/resource_service.hpp"
#include "engine/function/scene/prefab_property_override.hpp"
#include "engine/function/scene/scene_types.hpp"
#include "engine/resource/scene/scene_codec.hpp"

#include "engine/core/identifier_conversion.hpp"
#include "engine/core/meta/registration.hpp"
#include "engine/function/world/component_identity.hpp"

#include "engine/function/world/world.hpp"
#include "engine/function/world/world_reflection.hpp"

#include "engine/function/scene/scene_object.hpp"

#include "engine/function/scene/structural_diff.hpp"
#include "engine/resource/scene/scene_description.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>
#include <map>
#include <new>
#include <ranges>
#include <unordered_map>
#include <unordered_set>

namespace gneiss::engine {
namespace {
// 创建与重载操作的临时输入；不会进入场景成员列表。
struct staged_object {
  gneiss::engine::guid uuid;
  std::uint64_t entity{};
  scene_node_id node{};
  engine::mesh mesh;
  engine::material material;
};
error commit_object(world& native_world, const object_description& source, staged_object& target,
                    scene_node_id parent, resource_service& resources) {
  auto created =
      create_scene_object(native_world, source, parent, {target.mesh, target.material}, &resources);
  if (!created)
    return created.error();
  target.node = *created;
  target.entity = scene_object_entity(native_world, target.node);
  return error::success;
}

[[nodiscard]] bool equal_prefab_instances(const object_description& left,
                                          const object_description& right) {
  if (left.uuid != right.uuid || left.name != right.name || left.parent_uuid != right.parent_uuid ||
      left.prefab->uri != right.prefab->uri ||
      left.transform.translation != right.transform.translation ||
      left.transform.rotation != right.transform.rotation ||
      left.transform.scale != right.transform.scale ||
      left.prefab->overrides.size() != right.prefab->overrides.size()) {
    return false;
  }
  if (left.prefab->component_overrides.size() != right.prefab->component_overrides.size())
    return false;
  for (std::size_t i = 0; i < left.prefab->component_overrides.size(); ++i) {
    const auto& a = left.prefab->component_overrides[i];
    const auto& b = right.prefab->component_overrides[i];
    if (a.source_node_uuid != b.source_node_uuid || a.type_guid != b.type_guid ||
        bool(a.added) != bool(b.added))
      return false;
    if (a.added && !equal_components({*a.added}, {*b.added}))
      return false;
  }
  for (std::size_t index = 0U; index < left.prefab->overrides.size(); ++index) {
    if (!(left.prefab->overrides[index].key == right.prefab->overrides[index].key) ||
        !(left.prefab->overrides[index].value == right.prefab->overrides[index].value)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool equal_prefab_references(const scene_description& left,
                                           const scene_description& right) {
  const auto filter =
      std::views::filter([](const auto& object) { return object.prefab.has_value(); });
  return std::ranges::equal(left.objects | filter, right.objects | filter, equal_prefab_instances);
}

} // namespace

scene_instance::scene_instance(world& native_world, resource_service& loader) noexcept
    : native_world_(native_world), loader_(loader) {}

scene_instance::~scene_instance() noexcept { rollback(); }

std::vector<scene_instance::node_entry>::iterator
scene_instance::find_authored_node(scene_node_id node) {
  return std::ranges::find_if(
      objects_, [node](const auto& value) { return value.node == node && value.persistent; });
}
std::vector<scene_instance::node_entry>::iterator
scene_instance::find_ordinary_node(scene_node_id node) {
  auto found = find_authored_node(node);
  return std::ranges::any_of(prefab_instances_, [node](const auto& p) { return p->root() == node; })
             ? objects_.end()
             : found;
}
std::vector<scene_instance::node_entry>::const_iterator
scene_instance::find_ordinary_node(scene_node_id node) const {
  auto found = std::ranges::find_if(
      objects_, [node](const auto& value) { return value.node == node && value.persistent; });
  return std::ranges::any_of(prefab_instances_, [node](const auto& p) { return p->root() == node; })
             ? objects_.end()
             : found;
}
prefab_reference_description& scene_instance::prefab_reference(std::size_t index) {
  return prefab_instances_.at(index)->reference_;
}
const prefab_reference_description& scene_instance::prefab_reference(std::size_t index) const {
  return prefab_instances_.at(index)->reference_;
}
void scene_instance::append_prefab_members(const prefab_binding& prefab) {
  objects_.reserve(objects_.size() + prefab.nodes_.size());
  for (const auto& source : prefab.nodes_)
    if (source.node != prefab.root())
      objects_.push_back({source.node, false});
}
void scene_instance::append_staged_prefab(std::unique_ptr<prefab_binding> candidate,
                                          prefab_reference_description reference,
                                          std::size_t index) {
  prefab_instances_.reserve(prefab_instances_.size() + 1U);
  candidate->reference_ = std::move(reference);
  append_prefab_members(*candidate);
  objects_.at(index) = {candidate->root(), true};
  prefab_instances_.push_back(std::move(candidate));
}
void scene_instance::initialize_staged(guid uuid, std::size_t count) {
  uuid_ = uuid;
  objects_.resize(count);
  prefab_instances_.reserve(count);
}
engine::error scene_instance::create_staged_node(std::size_t index,
                                                 const object_description& source,
                                                 scene_node_id parent, engine::mesh mesh,
                                                 engine::material material) {
  if (index >= objects_.size() || objects_[index].node)
    return error::invalid_state;

  auto created = create_scene_object(native_world_, source, parent, {mesh, material}, &loader_);
  if (!created)
    return created.error();
  objects_[index].node = *created;
  return error::success;
}
void scene_instance::rollback() noexcept {
  for (const auto& object : objects_) {
    if (object.node)
      (void)native_world_.object_destroy(object.node);
  }
  objects_.clear();
  prefab_instances_.clear();
  loader_.collect_unused();
}
scene_node_id scene_instance::find_node(gneiss::engine::guid uuid) const noexcept {
  for (const auto& object : objects_)
    if (object.persistent && scene_object_identity(native_world_, object.node).uuid == uuid)
      return object.node;
  return {};
}

engine::error scene_instance::reload(scene_description candidate) {
  auto captured = capture_description();
  if (!captured)
    return captured.error();
  const auto& current = *captured;
  if (candidate.uuid != current.uuid || !equal_prefab_references(current, candidate)) {
    return engine::error::invalid_argument;
  }

  structural_diff diff;
  auto result = build_structural_diff(current.objects, candidate.objects, diff);
  if (result != engine::error::success) {
    return result;
  }

  std::vector<staged_object> staged;
  staged.reserve(candidate.objects.size());
  std::unordered_map<guid, std::size_t, guid_hash> old_indices;
  old_indices.reserve(objects_.size());
  for (std::size_t index = 0U; index < objects_.size(); ++index) {
    if (objects_[index].persistent)
      old_indices.emplace(scene_object_identity(native_world_, objects_[index].node).uuid, index);
  }
  for (const auto& source : candidate.objects) {
    staged_object target;
    target.uuid = source.uuid;
    if (const auto previous = old_indices.find(source.uuid); previous != old_indices.end()) {
      target.entity = scene_object_entity(native_world_, objects_[previous->second].node);
      target.node = objects_[previous->second].node;
    }
    auto renderer = resolve_component_renderer(source.components, loader_);
    if (!renderer)
      return renderer.error();
    target.mesh = renderer->mesh;
    target.material = renderer->material;
    staged.push_back(std::move(target));
  }

  std::vector<node_entry> members;
  members.reserve(staged.size() + objects_.size());
  std::vector<scene_node_identity> identities;
  identities.reserve(candidate.objects.size());
  for (const auto& source : candidate.objects)
    identities.push_back({source.uuid, source.name});
  std::unordered_map<guid, scene_node_id, guid_hash> nodes;
  nodes.reserve(staged.size());
  for (const auto& target : staged) {
    if (target.node != null_scene_node_id) {
      nodes.emplace(target.uuid, target.node);
    }
  }

  std::vector<std::size_t> committed_additions;
  committed_additions.reserve(diff.added.size());
  std::uint64_t active_camera_before = UINT64_C(0);
  (void)engine::write_identifier<entity_id>(active_camera_before, [&](auto& native_output) {
    return native_world_.get_active_camera(native_output);
  });
  const auto rollback_additions = [&]() noexcept {
    for (auto iterator = committed_additions.rbegin(); iterator != committed_additions.rend();
         ++iterator) {
      auto& target = staged[*iterator];
      if (target.node) {
        (void)native_world_.object_destroy(target.node);
      }
      target.entity = UINT64_C(0);
      target.node = null_scene_node_id;
    }
    if (active_camera_before != UINT64_C(0)) {
      (void)native_world_.set_active_camera(gneiss::entity_id{active_camera_before});
    }
  };
  for (const auto& addition : diff.added) {
    const auto& source = candidate.objects[addition.new_index];
    const auto parent = source.parent_uuid ? nodes.at(*source.parent_uuid) : null_scene_node_id;
    result = commit_object(native_world_, source, staged[addition.new_index], parent, loader_);
    if (result != engine::error::success) {
      auto& failed = staged[addition.new_index];
      if (failed.node) {
        (void)native_world_.object_destroy(failed.node);
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
    scene_node_id parent;
    math::transform transform;
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

    const auto& source = candidate.objects[update.new_index];
    if (has_change(update.changes, structural_change::parent)) {
      const auto parent = source.parent_uuid ? nodes.at(*source.parent_uuid) : null_scene_node_id;
      result = native_world_.node_reparent(gneiss::scene_node_id{target.node},
                                           gneiss::scene_node_id{parent});
    }
    if (result == engine::error::success &&
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

  // 已提前预留容量，提交成员关系不再分配。
  for (const auto& value : staged)
    members.push_back({value.node});
  for (const auto& removal : diff.removed) {
    result = native_world_.object_destroy(find_node(current.objects[removal.old_index].uuid));
    if (result.failed())
      return result;
  }
  for (std::size_t index = 0; index < staged.size(); ++index) {
    result = native_world_.node_set_identity(staged[index].node, std::move(identities[index]));
    if (result.failed())
      return result;
  }
  for (const auto& member : objects_)
    if (!member.persistent)
      members.push_back(member);
  objects_ = std::move(members);
  applied.clear();
  staged.clear();
  loader_.collect_unused();
  return engine::error::success;
}

engine::error scene_instance::reload_prefab(std::string_view uri, prefab_asset_ref candidate,
                                            scene_diagnostic* diagnostic) {
  std::vector<std::size_t> affected;
  affected.reserve(prefab_instances_.size());
  for (std::size_t index = 0U; index < prefab_instances_.size(); ++index) {
    if (prefab_reference(index).uri == uri) {
      affected.push_back(index);
    }
  }
  if (affected.empty()) {
    return engine::error::not_found;
  }

  auto result = engine::error::success;
  for (const auto index : affected) {
    result =
        prefab_instances_[index]->validate_reload(candidate, prefab_reference(index), diagnostic);
    if (result != engine::error::success) {
      return result;
    }
  }
  for (const auto index : affected) {
    objects_.reserve(objects_.size() + candidate->objects.size() + 1U);
    auto& prefab = *prefab_instances_[index];
    std::vector<scene_node_id> previous;
    previous.reserve(prefab.nodes_.size() + 1U);
    for (const auto& node : prefab.nodes_)
      if (node.node != prefab.root())
        previous.push_back(node.node);
    result = prefab.reload(candidate, prefab_reference(index));
    if (result.ok()) {
      std::erase_if(objects_, [&](const auto& object) {
        return std::ranges::find(previous, object.node) != previous.end();
      });
      append_prefab_members(prefab);
    }
    if (result != engine::error::success) {
      return result;
    }
  }
  return engine::error::success;
}

result<scene_node_info> scene_instance::get_node_info(std::uint64_t index,
                                                      node_query_detail detail) const {
  if (index >= objects_.size()) {
    return engine::error::not_found;
  }
  const auto& object = objects_[static_cast<std::size_t>(index)];
  std::uint64_t entity = UINT64_C(0);
  auto result = engine::write_identifier<entity_id>(entity, [&](auto& native_output) {
    return native_world_.node_get_entity(gneiss::scene_node_id{object.node}, native_output);
  });
  if (result != engine::error::success || entity == UINT64_C(0)) {
    return result == engine::error::success ? engine::error::invalid_handle : result;
  }
  scene_node_id parent = null_scene_node_id;
  result = native_world_.node_get_parent(gneiss::scene_node_id{object.node}, parent);
  if (result != engine::error::success) {
    return result;
  }
  scene_node_info out_info{};
  out_info.node = scene_node_id{object.node};
  out_info.parent = scene_node_id{parent};
  out_info.entity = entity_id{entity};
  out_info.uuid = scene_object_identity(native_world_, object.node).uuid;
  out_info.name = scene_object_identity(native_world_, object.node).name;
  out_info.prefab = prefab_origin(object.node);
  if (detail != node_query_detail::identity) {
    auto components = capture_components(native_world_, object.node);
    if (!components)
      return components.error();
    out_info.components = std::move(*components);
  }
  if (detail == node_query_detail::components) {
    result = native_world_.node_get_local_transform(object.node, out_info.local_transform);
    if (result.failed())
      return result;
  }
  return out_info;
}

std::optional<prefab_origin_view> scene_instance::prefab_origin(scene_node_id node) const {
  for (std::size_t index = 0; index < prefab_instances_.size(); ++index) {
    const auto& runtime = *prefab_instances_[index];
    const auto& reference = prefab_reference(index);
    prefab_origin_view origin;
    origin.instance_uuid = runtime.instance_uuid_;
    origin.prefab_uri = reference.uri;
    const auto found = std::ranges::find(runtime.nodes_, node, &prefab_binding::runtime_node::node);
    if (found == runtime.nodes_.end())
      continue;
    origin.is_instance_root = runtime.root() == node;
    origin.overrides = reference.overrides;
    origin.source_node_uuid = found->address.source_node_uuid;
    if (const auto* source = runtime.find_source_object(origin.source_node_uuid))
      origin.source_local_transform = source->transform;
    return origin;
  }
  return std::nullopt;
}

engine::error scene_instance::set_prefab_source_transform(scene_node_id node,
                                                          const math::transform& transform) {
  for (std::size_t index = 0U; index < prefab_instances_.size(); ++index) {
    auto& runtime = *prefab_instances_[index];
    if (runtime.root() == node)
      return error::invalid_argument;
    prefab_binding::node_info info;
    for (std::size_t node_index = 0U; node_index < runtime.node_count(); ++node_index) {
      if (runtime.get_node_info(node_index, info) != engine::error::success || info.node != node) {
        continue;
      }
      const auto* source = runtime.find_source_object(info.address->source_node_uuid);
      if (source == nullptr) {
        return engine::error::invalid_state;
      }
      auto pending = prefab_reference(index).overrides;
      const auto type_id = engine::transform_component_type;
      const std::array candidates{engine::prefab_property_override{
                                      .key = {.source_node_uuid = info.address->source_node_uuid,
                                              .type_guid = type_id,
                                              .property_id = transform_translation_field},
                                      .value = {.payload = transform.translation}},
                                  engine::prefab_property_override{
                                      .key = {.source_node_uuid = info.address->source_node_uuid,
                                              .type_guid = type_id,
                                              .property_id = transform_rotation_field},
                                      .value = {.payload = transform.rotation}},
                                  engine::prefab_property_override{
                                      .key = {.source_node_uuid = info.address->source_node_uuid,
                                              .type_guid = type_id,
                                              .property_id = transform_scale_field},
                                      .value = {.payload = transform.scale}}};
      const std::array source_values{engine::value{.payload = source->transform.translation},
                                     engine::value{.payload = source->transform.rotation},
                                     engine::value{.payload = source->transform.scale}};
      for (std::size_t field_index = 0U; field_index < candidates.size(); ++field_index) {
        const auto result = set_prefab_property_override(pending, candidates[field_index],
                                                         source_values[field_index]);
        if (result != engine::error::success) {
          return result;
        }
      }
      const auto result =
          native_world_.node_set_local_transform(gneiss::scene_node_id{node}, transform);
      if (result != engine::error::success) {
        return result;
      }
      prefab_reference(index).overrides.swap(pending);
      return engine::error::success;
    }
  }
  return engine::error::invalid_handle;
}

result<scene_node_id> scene_instance::create_node(const node_creation& desc) {
  const auto uuid = desc.uuid;
  const auto name = desc.name;
  if (!uuid || find_node(uuid) != null_scene_node_id) {
    return engine::error::invalid_argument;
  }
  std::optional<gneiss::engine::guid> parent_uuid;
  if (desc.parent != null_scene_node_id) {
    const auto parent = find_authored_node(desc.parent);
    if (parent == objects_.end()) {
      return engine::error::invalid_handle;
    }
    parent_uuid = scene_object_identity(native_world_, parent->node).uuid;
  }
  try {
    object_description author{.uuid = uuid,
                              .name = std::string(name),
                              .parent_uuid = std::move(parent_uuid),
                              .transform = desc.local_transform,
                              .components = {}};
    objects_.reserve(objects_.size() + 1U);
    staged_object target{.uuid = author.uuid,
                         .entity = UINT64_C(0),
                         .node = null_scene_node_id,
                         .mesh = {},
                         .material = {}};
    const auto result = commit_object(native_world_, author, target, desc.parent, loader_);
    if (result != engine::error::success) {
      if (target.node) {
        (void)native_world_.object_destroy(target.node);
      }
      return result;
    }
    objects_.push_back({target.node});
    return target.node;
  } catch (const std::bad_alloc&) {
    return engine::error::out_of_memory;
  } catch (...) {
    return engine::error::internal;
  }
}

engine::error scene_instance::set_node_name(scene_node_id node, std::string_view name) {
  const auto found = find_authored_node(node);
  if (found == objects_.end()) {
    return engine::error::invalid_handle;
  }
  auto identity = native_world_.node_identity(node);
  if (!identity)
    return identity.error();
  try {
    return native_world_.node_set_identity(node, {identity->uuid, std::string(name)});
  } catch (const std::bad_alloc&) {
    return error::out_of_memory;
  }
}

engine::error scene_instance::reparent_node(scene_node_id node, scene_node_id parent) {
  const auto found = find_authored_node(node);
  if (found == objects_.end()) {
    return engine::error::invalid_handle;
  }
  if (parent && find_authored_node(parent) == objects_.end())
    return error::invalid_handle;
  return native_world_.node_reparent(node, parent);
}

result<scene_subtree_snapshot> scene_instance::capture_subtree(scene_node_id root) const {
  if (!native_world_.is_owner_thread())
    return error::invalid_state;
  if (!std::ranges::any_of(objects_, [root](const auto& member) {
        return member.node == root && member.persistent;
      }))
    return error::invalid_handle;
  scene_subtree_snapshot snapshot;
  auto description = capture_description();
  if (!description)
    return description.error();
  snapshot.description = std::move(*description);
  snapshot.root_uuid = scene_object_identity(native_world_, root).uuid;
  const auto root_description = std::ranges::find(snapshot.description.objects, snapshot.root_uuid,
                                                  &object_description::uuid);
  if (root_description == snapshot.description.objects.end())
    return error::invalid_state;
  snapshot.parent_uuid = root_description->parent_uuid.value_or(guid{});
  std::unordered_set<guid, guid_hash> included{snapshot.root_uuid};
  bool changed = true;
  while (changed) {
    changed = false;
    for (const auto& object : snapshot.description.objects)
      if (object.parent_uuid && included.contains(*object.parent_uuid))
        changed |= included.emplace(object.uuid).second;
  }
  if (included.size() > scene_subtree_max_nodes)
    return error::unsupported;
  std::erase_if(snapshot.description.objects,
                [&](const auto& value) { return !included.contains(value.uuid); });
  for (auto& author : snapshot.description.objects) {
    if (author.uuid == snapshot.root_uuid)
      author.parent_uuid.reset();
    if (author.prefab) {
      auto& resources = snapshot.resources[author.uuid];
      const auto binding = std::ranges::find_if(prefab_instances_, [&](const auto& value) {
        return value->instance_uuid_ == author.uuid;
      });
      if (binding == prefab_instances_.end())
        return error::invalid_state;
      resources.prefab = (*binding)->prefab_lease();
      resources.source_nodes.resize((*binding)->node_count());
      for (std::size_t i = 0; i < resources.source_nodes.size(); ++i) {
        auto binding = capture_prefab_instance_binding(author.uuid, i);
        if (!binding)
          return binding.error();
        resources.source_nodes[i] = std::move(*binding);
      }
    } else if (author.components.template get<mesh_renderer_description>()) {
      const auto renderer = native_world_.node_mesh_renderer(find_node(author.uuid));
      if (!renderer)
        return renderer.error();
      snapshot.resources[author.uuid].renderer = {renderer->mesh, renderer->material};
    }
  }
  return snapshot;
}

result<scene_node_id> scene_instance::restore_subtree(const scene_subtree_snapshot& snapshot,
                                                      scene_node_id parent,
                                                      std::span<const uuid_mapping> mappings) {
  if (!native_world_.is_owner_thread())
    return error::invalid_state;
  if (parent && find_authored_node(parent) == objects_.end())
    return error::invalid_handle;
  auto subtree = snapshot.description;
  if (subtree.objects.empty() || subtree.objects.size() > scene_subtree_max_nodes)
    return error::invalid_argument;
  structural_diff order;
  auto status = build_structural_diff({}, subtree.objects, order);
  if (status.failed())
    return status;
  const auto root =
      std::ranges::find(subtree.objects, snapshot.root_uuid, &object_description::uuid);
  if (root == subtree.objects.end() || root->parent_uuid ||
      std::ranges::count_if(subtree.objects, [](const auto& o) { return !o.parent_uuid; }) != 1)
    return error::invalid_argument;
  std::map<guid, guid> remap;
  std::unordered_set<guid, guid_hash> targets;
  if (!mappings.empty() && mappings.size() != subtree.objects.size())
    return error::invalid_argument;
  for (const auto& source : subtree.objects) {
    const auto mapping = std::ranges::find(mappings, source.uuid, &uuid_mapping::source_uuid);
    if (!mappings.empty() && mapping == mappings.end())
      return error::invalid_argument;
    auto target = mappings.empty() ? source.uuid : mapping->target_uuid;
    if (!target || find_node(target) || !targets.emplace(target).second)
      return error::invalid_argument;
    remap.emplace(source.uuid, std::move(target));
  }
  // 临时成员集合只负责本次创建失败的清理，不注册第二个场景实例。
  scene_instance staged(native_world_, loader_);
  staged.uuid_ = uuid_;
  entity_id old_camera;
  (void)native_world_.get_active_camera(old_camera);
  struct camera_guard {
    world& owner;
    entity_id previous;
    bool committed{};
    ~camera_guard() {
      if (!committed && previous.is_valid())
        (void)owner.set_active_camera(previous);
    }
  } camera{native_world_, old_camera};
  for (const auto& entry : order.added) {
    const auto& original = subtree.objects[entry.new_index];
    auto author = original;
    author.uuid = remap.at(original.uuid);
    if (author.parent_uuid)
      author.parent_uuid = remap.at(*author.parent_uuid);
    if (!mappings.empty() && author.components.template get<camera_description>())
      author.components.update<camera_description>([&](auto& value) { value.is_primary = false; });
    const auto asset = snapshot.resources.find(original.uuid);
    const auto node_parent =
        author.parent_uuid ? staged.find_node(*author.parent_uuid) : scene_node_id{};
    if (author.prefab) {
      if (asset == snapshot.resources.end() || !asset->second.prefab)
        return error::not_found;
      auto created = staged.instantiate_prefab_ready(author, asset->second.prefab,
                                                     asset->second.source_nodes, false);
      if (!created)
        return created.error();
      status = error::success;
    } else {
      scene_mesh_binding resources;
      if (author.components.template get<mesh_renderer_description>()) {
        if (asset == snapshot.resources.end())
          return error::not_found;
        resources = asset->second.renderer;
        if (resource_uri(resources.mesh) !=
                author.components.template get<mesh_renderer_description>()->mesh_uri ||
            resource_uri(resources.material) !=
                author.components.template get<mesh_renderer_description>()->material_uri)
          return error::not_found;
        if ((resources.mesh && !loader_.owns(resources.mesh)) ||
            (resources.material && !loader_.owns(resources.material)))
          return error::invalid_handle;
      }
      // 先扩容，保证创建成功后登记成员不会因分配失败遗漏节点。
      staged.objects_.reserve(staged.objects_.size() + 1);
      auto created = create_scene_object(native_world_, author, node_parent,
                                         {resources.mesh, resources.material});
      if (!created)
        return created.error();
      staged.objects_.push_back({*created});
      status = error::success;
    }
    if (status.failed())
      return status;
  }
  const auto created_root = staged.find_node(remap.at(snapshot.root_uuid));
  // 合并前完成分配，提交后临时集合放弃节点所有权。
  objects_.reserve(objects_.size() + staged.objects_.size());
  prefab_instances_.reserve(prefab_instances_.size() + staged.prefab_instances_.size());
  status = native_world_.node_reparent(created_root, parent);
  if (status.failed())
    return status;
  objects_.insert(objects_.end(), staged.objects_.begin(), staged.objects_.end());
  for (auto& binding : staged.prefab_instances_)
    prefab_instances_.push_back(std::move(binding));
  staged.objects_.clear();
  staged.prefab_instances_.clear();
  camera.committed = true;
  return created_root;
}

engine::error scene_instance::destroy_subtree(scene_node_id root) {
  const auto member = std::ranges::find(objects_, root, &node_entry::node);
  if (member == objects_.end())
    return error::invalid_handle;
  const auto origin = prefab_origin(root);
  if (origin && !origin->is_instance_root)
    return error::invalid_state;
  try {
    std::vector<scene_node_id> removed;
    removed.reserve(objects_.size());
    for (const auto& member : objects_) {
      auto current = member.node;
      while (current) {
        if (current == root) {
          removed.push_back(member.node);
          break;
        }
        const auto status = native_world_.node_get_parent(current, current);
        if (status.failed())
          return status;
      }
    }
    const auto contains = [&](scene_node_id node) {
      return std::ranges::find(removed, node) != removed.end();
    };
    std::vector<std::size_t> prefabs;
    for (std::size_t index = 0; index < prefab_instances_.size(); ++index)
      if (contains(prefab_instances_[index]->root()))
        prefabs.push_back(index);
    const auto status = native_world_.object_destroy(root);
    if (status.failed())
      return status;
    std::erase_if(objects_, [&](const auto& object) { return contains(object.node); });
    for (auto it = prefabs.rbegin(); it != prefabs.rend(); ++it) {
      prefab_instances_.erase(prefab_instances_.begin() + static_cast<std::ptrdiff_t>(*it));
    }
    loader_.collect_unused();
    return error::success;
  } catch (const std::bad_alloc&) {
    return error::out_of_memory;
  }
}

result<scene_node_id>
scene_instance::create_mesh_renderer_node(const mesh_renderer_node_creation& desc,
                                          const scene_mesh_binding* prepared) {
  if (prepared &&
      (engine::resource_kind(prepared->mesh) != engine::render_resource_kind::mesh ||
       engine::resource_kind(prepared->material) != engine::render_resource_kind::material ||
       !loader_.owns(prepared->mesh) || !loader_.owns(prepared->material) ||
       engine::resource_uri(prepared->mesh) != desc.mesh_uri ||
       engine::resource_uri(prepared->material) != desc.material_uri)) {
    return engine::error::invalid_argument;
  }
  const auto uuid = desc.uuid;
  const auto name = desc.name;
  const auto mesh_uri = desc.mesh_uri;
  const auto material_uri = desc.material_uri;
  if (!uuid || find_node(uuid) != null_scene_node_id) {
    return engine::error::invalid_argument;
  }
  std::optional<gneiss::engine::guid> parent_uuid;
  if (desc.parent != null_scene_node_id) {
    const auto parent = find_authored_node(desc.parent);
    if (parent == objects_.end()) {
      return engine::error::invalid_handle;
    }
    parent_uuid = scene_object_identity(native_world_, parent->node).uuid;
  }

  try {
    object_description author{
        .uuid = uuid,
        .name = std::string(name),
        .parent_uuid = std::move(parent_uuid),
        .transform = {},
        .components = {engine::mesh_renderer_description{
            .mesh_uri = std::string(mesh_uri), .material_uri = std::string(material_uri)}}};
    objects_.reserve(objects_.size() + 1U);
    staged_object target{};
    target.uuid = author.uuid;

    auto result = engine::error::success;
    if (prepared) {
      target.mesh = prepared->mesh;
      target.material = prepared->material;
    } else {
      result = engine::error::success;
      {
        auto loaded = loader_.find<mesh_resource>(mesh_uri);
        if (loaded)
          target.mesh = std::move(*loaded);
        else {
          target.mesh = {};
          result = loaded.error();
        }
      };
      if (result == engine::error::success) {
        result = engine::error::success;
        {
          auto loaded = loader_.find<material_resource>(material_uri);
          if (loaded)
            target.material = std::move(*loaded);
          else {
            target.material = {};
            result = loaded.error();
          }
        };
      }
    }
    if (result != engine::error::success) {
      return result;
    }
    result = commit_object(native_world_, author, target, desc.parent, loader_);
    if (result != engine::error::success) {
      if (target.node) {
        (void)native_world_.object_destroy(target.node);
      }
      return result;
    }
    objects_.push_back({target.node});
    return target.node;
  } catch (const std::bad_alloc&) {
    return engine::error::out_of_memory;
  } catch (...) {
    return engine::error::internal;
  }
}

engine::error scene_instance::set_mesh_renderer(scene_node_id node, std::string_view mesh_uri,
                                                std::string_view material_uri) {
  const auto found = find_ordinary_node(node);
  if (found == objects_.end()) {
    return engine::error::invalid_handle;
  }
  try {
    auto current = native_world_.node_mesh_renderer(node);
    if (!current && current.error() != error::not_found)
      return current.error();
    const auto binding = current ? *current : mesh_renderer{};
    // 编辑资产选择不等于重载；未改变的 URI 继续使用节点选定的版本。
    auto mesh = engine::resource_uri(binding.mesh) == mesh_uri ? binding.mesh : engine::mesh{};
    auto material = engine::resource_uri(binding.material) == material_uri ? binding.material
                                                                           : engine::material{};

    auto result = engine::error::success;
    if (!mesh && !mesh_uri.empty()) {
      result = engine::error::success;
      {
        auto loaded = loader_.find<mesh_resource>(mesh_uri);
        if (loaded)
          mesh = std::move(*loaded);
        else {
          mesh = {};
          result = loaded.error();
        }
      };
    }
    if (result == engine::error::success && !material && !material_uri.empty()) {
      result = engine::error::success;
      {
        auto loaded = loader_.find<material_resource>(material_uri);
        if (loaded)
          material = std::move(*loaded);
        else {
          material = {};
          result = loaded.error();
        }
      };
    }
    if (result != engine::error::success) {
      return result;
    }
    const engine::mesh_renderer renderer{.mesh = mesh, .material = material};
    result = native_world_.entity_set_mesh_renderer(
        gneiss::entity_id{scene_object_entity(native_world_, found->node)}, renderer);
    if (result != engine::error::success) {
      return result;
    }
    loader_.collect_unused();
    return engine::error::success;
  } catch (const std::bad_alloc&) {
    return engine::error::out_of_memory;
  } catch (...) {
    return engine::error::internal;
  }
}

engine::error scene_instance::set_camera(scene_node_id node,
                                         const engine::camera_description& desc) try {
  // Prefab 的相机参数只写实例覆盖，不修改共享来源描述或活动相机。
  for (std::size_t index = 0; index < prefab_instances_.size(); ++index) {
    auto& runtime = *prefab_instances_[index];
    for (std::size_t n = 0; n < runtime.node_count(); ++n) {
      prefab_binding::node_info info;
      if (runtime.get_node_info(n, info).failed() || info.node != node)
        continue;
      const auto* source = runtime.find_source_object(info.address->source_node_uuid);
      const auto* camera = source ? source->components.get<camera_description>() : nullptr;
      if (!camera)
        return set_component_description(node, component_description{desc});
      entity_id active;
      auto status = native_world_.get_active_camera(active);
      if (status.failed() && status != error::not_ready)
        return status;
      const auto entity = entity_id{scene_object_entity(native_world_, node)};
      if (desc.is_primary != (active == entity))
        return error::unsupported;
      auto pending = prefab_reference(index).overrides;
      const std::array fields{camera_vertical_field_of_view_field, camera_near_plane_field,
                              camera_far_plane_field};
      const std::array values{desc.settings.fov, desc.settings.near, desc.settings.far};
      const std::array originals{camera->settings.fov, camera->settings.near, camera->settings.far};
      for (std::size_t i = 0; i < fields.size(); ++i) {
        status = set_prefab_property_override(
            pending,
            {.key = {.source_node_uuid = info.address->source_node_uuid,
                     .type_guid = camera_component_type,
                     .property_id = fields[i]},
             .value = engine::value{values[i]}},
            engine::value{originals[i]});
        if (status.failed())
          return status;
      }
      status = native_world_.entity_configure_camera(entity, desc.settings);
      if (status.failed())
        return status;
      prefab_reference(index).overrides.swap(pending);
      return error::success;
    }
  }
  const auto found = find_ordinary_node(node);
  if (found == objects_.end()) {
    return engine::error::invalid_handle;
  }
  const auto result = native_world_.entity_configure_camera(
      gneiss::entity_id{scene_object_entity(native_world_, found->node)}, desc.settings);
  if (result != engine::error::success) {
    return result;
  }
  if (desc.is_primary) {
    const auto active_result = native_world_.set_active_camera(
        gneiss::entity_id{scene_object_entity(native_world_, found->node)});
    if (active_result != engine::error::success) {
      return active_result;
    }
  }
  return engine::error::success;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}

engine::error scene_instance::remove_camera(scene_node_id node) {
  const auto found = find_ordinary_node(node);
  if (found == objects_.end()) {
    return engine::error::invalid_handle;
  }
  return native_world_.entity_remove_camera(
      gneiss::entity_id{scene_object_entity(native_world_, found->node)});
}

engine::error scene_instance::remove_mesh_renderer(scene_node_id node) {
  const auto found = find_ordinary_node(node);
  if (found == objects_.end()) {
    return engine::error::invalid_handle;
  }
  const auto result = native_world_.entity_remove_mesh_renderer(
      gneiss::entity_id{scene_object_entity(native_world_, found->node)});
  if (result != engine::error::success) {
    return result;
  }
  loader_.collect_unused();
  return engine::error::success;
}

engine::error scene_instance::destroy_node(scene_node_id node) {
  if (find_authored_node(node) == objects_.end())
    return error::invalid_handle;
  // 单节点删除始终要求没有实际子节点；整棵实例使用 destroy_subtree。
  for (const auto& member : objects_) {
    scene_node_id parent;
    const auto status = native_world_.node_get_parent(member.node, parent);
    if (status.failed())
      return status;
    if (parent == node)
      return error::invalid_state;
  }
  return destroy_subtree(node);
}

result<scene_description> scene_instance::capture_description() const {
  scene_description current;
  current.uuid = uuid_;
  current.objects.reserve(objects_.size());
  std::unordered_set<guid, guid_hash> persistent_ids;
  persistent_ids.reserve(objects_.size());
  for (const auto& member : objects_) {
    if (!member.persistent)
      continue;
    auto identity = native_world_.node_identity(member.node);
    if (!identity)
      return identity.error();
    if (!persistent_ids.insert(identity->uuid).second)
      return error::invalid_state;
  }
  for (const auto& member : objects_) {
    if (!member.persistent)
      continue;
    const auto prefab = std::ranges::find_if(
        prefab_instances_, [&](const auto& p) { return p->root() == member.node; });
    auto captured =
        capture_scene_object(native_world_, member.node, prefab == prefab_instances_.end());
    if (!captured)
      return captured.error();
    if (captured->parent_uuid && !persistent_ids.contains(*captured->parent_uuid))
      return error::invalid_state; // 不将跨场景或临时父节点身份写进文件。
    if (prefab != prefab_instances_.end())
      captured->prefab = (*prefab)->reference_;
    current.objects.push_back(std::move(*captured));
  }
  return current;
}

result<std::string> scene_instance::serialize() const {
  auto current = capture_description();
  if (!current)
    return current.error();
  return serialize_scene_description(*current);
}

} // namespace gneiss::engine
