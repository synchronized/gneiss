// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/scene/scene_instances.hpp"
#include "engine/core/error.hpp"
#include "engine/core/scene_types.hpp"
#include "engine/function/resource/resource_service.hpp"
#include "engine/function/scene/prefab_property_override.hpp"
#include "engine/resource/scene/scene_codec.hpp"

#include "engine/core/identifier_conversion.hpp"
#include "engine/core/meta/registration.hpp"
#include "engine/function/world/component_identity.hpp"

#include "engine/function/world/world.hpp"
#include "engine/function/world/world_reflection.hpp"

#include "engine/function/scene/scene_instance.hpp"
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

std::uint16_t allocate_domain() noexcept {
  static std::atomic_uint32_t next_domain{UINT32_C(1024)};
  const auto value = next_domain.fetch_add(1U, std::memory_order_relaxed);
  return value <= std::numeric_limits<std::uint16_t>::max() ? static_cast<std::uint16_t>(value)
                                                            : UINT16_C(0);
}

// 创建与重载操作的临时输入；不会进入场景成员列表。
struct staged_object {
  gneiss::engine::guid uuid;
  std::uint64_t entity{};
  scene_node_id node{};
  engine::mesh mesh;
  engine::material material;
};
error commit_object(world& native_world, const object_description& source, staged_object& target,
                    scene_node_id parent) {
  auto created = create_scene_object(native_world, source, parent, {target.mesh, target.material});
  if (!created)
    return created.error();
  target.node = *created;
  target.entity = scene_object_entity(native_world, target.node);
  return error::success;
}

[[nodiscard]] bool equal_prefab_instances(const object_description& left,
                                          const object_description& right) noexcept {
  if (left.uuid != right.uuid || left.name != right.name || left.parent_uuid != right.parent_uuid ||
      left.prefab->uri != right.prefab->uri ||
      left.transform.translation != right.transform.translation ||
      left.transform.rotation != right.transform.rotation ||
      left.transform.scale != right.transform.scale ||
      left.prefab->overrides.size() != right.prefab->overrides.size()) {
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
                                           const scene_description& right) noexcept {
  const auto filter =
      std::views::filter([](const auto& object) { return object.prefab.has_value(); });
  return std::ranges::equal(left.objects | filter, right.objects | filter, equal_prefab_instances);
}

engine::error apply_camera(world& native_world, std::uint64_t entity,
                           const gneiss::engine::camera_description* camera) noexcept {
  if (!camera) {
    const auto result = native_world.entity_remove_camera(gneiss::entity_id{entity});
    return result == engine::error::not_found ? engine::error::success : result;
  }
  const auto& value = camera->settings;
  return native_world.entity_set_camera(gneiss::entity_id{entity}, value, camera->is_primary);
}

engine::error apply_mesh_renderer(world& native_world, staged_object& object,
                                  bool present) noexcept {
  if (!present) {
    const auto result = native_world.entity_remove_mesh_renderer(gneiss::entity_id{object.entity});
    return result == engine::error::not_found ? engine::error::success : result;
  }
  const engine::mesh_renderer renderer{.mesh = object.mesh, .material = object.material};
  return native_world.entity_set_mesh_renderer(gneiss::entity_id{object.entity}, renderer);
}

} // namespace

scene_instance::scene_instance(world& native_world, resource_service& loader) noexcept
    : native_world_(native_world), loader_(loader) {}

scene_instance::~scene_instance() noexcept { rollback(); }

std::vector<scene_instance::object>::iterator
scene_instance::find_authored_node(scene_node_id node) {
  auto end = objects_.begin() + static_cast<std::ptrdiff_t>(description_.objects.size());
  const auto found =
      std::find_if(objects_.begin(), end, [node](const auto& value) { return value.node == node; });
  return found == end ? objects_.end() : found;
}
std::vector<scene_instance::object>::iterator
scene_instance::find_ordinary_node(scene_node_id node) {
  auto end = objects_.begin() + static_cast<std::ptrdiff_t>(description_.objects.size());
  const auto found =
      std::find_if(objects_.begin(), end, [node](const auto& value) { return value.node == node; });
  return found == end ||
                 description_.objects[static_cast<std::size_t>(found - objects_.begin())].prefab
             ? objects_.end()
             : found;
}
std::vector<scene_instance::object>::const_iterator
scene_instance::find_ordinary_node(scene_node_id node) const {
  auto end = objects_.begin() + static_cast<std::ptrdiff_t>(description_.objects.size());
  const auto found =
      std::find_if(objects_.begin(), end, [node](const auto& value) { return value.node == node; });
  return found == end ||
                 description_.objects[static_cast<std::size_t>(found - objects_.begin())].prefab
             ? objects_.end()
             : found;
}
object_description& scene_instance::prefab_author(std::size_t index) {
  return *std::ranges::find(description_.objects, prefab_instances_.at(index)->instance_uuid_,
                            &object_description::uuid);
}
const object_description& scene_instance::prefab_author(std::size_t index) const {
  return *std::ranges::find(description_.objects, prefab_instances_.at(index)->instance_uuid_,
                            &object_description::uuid);
}
void scene_instance::append_prefab_members(const prefab_binding& prefab) {
  objects_.reserve(objects_.size() + prefab.nodes_.size() + 1U);
  const auto author =
      std::ranges::find(description_.objects, prefab.instance_uuid_, &object_description::uuid);
  objects_.at(static_cast<std::size_t>(author - description_.objects.begin())).node = prefab.root();
  for (const auto& source : prefab.nodes_)
    if (source.node != prefab.root())
      objects_.push_back({source.node});
}
void scene_instance::append_staged_prefab(std::unique_ptr<prefab_binding> candidate) {
  prefab_instances_.reserve(prefab_instances_.size() + 1U);
  append_prefab_members(*candidate);
  prefab_instances_.push_back(std::move(candidate));
}

void scene_instance::initialize_staged(scene_description candidate) {
  description_ = std::move(candidate);
  objects_.resize(description_.objects.size());
  prefab_instances_.reserve(description_.objects.size());
}

engine::error scene_instance::create_staged_node(std::size_t index, scene_node_id parent,
                                                 engine::mesh mesh, engine::material material) {
  if (index >= objects_.size() || objects_[index].node)
    return error::invalid_state;
  const auto& source = description_.objects[index];
  if (source.components.template get<mesh_renderer_description>()) {
    if (!mesh || !material)
      return error::not_ready;
    if (!loader_.owns(mesh) || !loader_.owns(material))
      return error::invalid_argument;
  }
  auto created = create_scene_object(native_world_, source, parent, {mesh, material});
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
  for (std::size_t index = 0; index < description_.objects.size(); ++index)
    if (scene_object_identity(native_world_, objects_[index].node).uuid == uuid)
      return objects_[index].node;
  return {};
}

engine::error scene_instance::reload(scene_description candidate) {
  if (candidate.uuid != description_.uuid || !equal_prefab_references(description_, candidate)) {
    return engine::error::invalid_argument;
  }

  structural_diff diff;
  auto result = build_structural_diff(description_.objects, candidate.objects, diff);
  if (result != engine::error::success) {
    return result;
  }

  std::vector<staged_object> staged;
  staged.reserve(candidate.objects.size());
  std::unordered_map<guid, std::size_t, guid_hash> old_indices;
  old_indices.reserve(objects_.size());
  for (std::size_t index = 0U; index < description_.objects.size(); ++index) {
    old_indices.emplace(scene_object_identity(native_world_, objects_[index].node).uuid, index);
  }
  for (const auto& source : candidate.objects) {
    staged_object target;
    target.uuid = source.uuid;
    if (const auto previous = old_indices.find(source.uuid); previous != old_indices.end()) {
      target.entity = scene_object_entity(native_world_, objects_[previous->second].node);
      target.node = objects_[previous->second].node;
    }
    if (source.components.template get<mesh_renderer_description>()) {
      result = [&] {
        render_asset_ref cached;
        auto status = engine::error::success;
        {
          auto loaded =
              loader_.find({source.components.template get<mesh_renderer_description>()->mesh_uri,
                            asset_type::mesh});
          if (loaded)
            cached = std::move(*loaded);
          else {
            cached = {};
            status = loaded.error();
          }
        };
        if (status.ok())
          target.mesh = asset_cast<mesh_resource>(cached);
        return status;
      }();
      if (result == engine::error::success) {
        result = [&] {
          render_asset_ref cached;
          auto status = engine::error::success;
          {
            auto loaded = loader_.find(
                {source.components.template get<mesh_renderer_description>()->material_uri,
                 asset_type::material});
            if (loaded)
              cached = std::move(*loaded);
            else {
              cached = {};
              status = loaded.error();
            }
          };
          if (status.ok())
            target.material = asset_cast<material_resource>(cached);
          return status;
        }();
      }
      if (result != engine::error::success) {
        return result;
      }
    }
    staged.push_back(std::move(target));
  }

  std::vector<object> members;
  members.reserve(staged.size() + objects_.size() - description_.objects.size());
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
    result = commit_object(native_world_, source, staged[addition.new_index], parent);
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
    std::optional<engine::camera_description> camera;
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
      if (has_change(snapshot.changes, structural_change::camera)) {
        (void)apply_camera(native_world_, target.entity,
                           snapshot.camera ? &*snapshot.camera : nullptr);
      }
      if (has_change(snapshot.changes, structural_change::mesh_renderer)) {
        if (snapshot.renderer.mesh) {
          (void)native_world_.entity_set_mesh_renderer(entity_id{target.entity}, snapshot.renderer);
        } else {
          (void)native_world_.entity_remove_mesh_renderer(entity_id{target.entity});
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
    result = native_world_.node_get_parent(gneiss::scene_node_id{target.node}, snapshot.parent);
    if (result == engine::error::success) {
      result = native_world_.node_get_local_transform(gneiss::scene_node_id{target.node},
                                                      snapshot.transform);
    }
    if (result == engine::error::success && has_change(update.changes, structural_change::camera)) {
      math::camera_settings camera;
      const auto camera_result =
          native_world_.entity_get_camera(gneiss::entity_id{target.entity}, camera);
      if (camera_result == engine::error::success) {
        std::uint64_t active = UINT64_C(0);
        const bool primary = engine::write_identifier<entity_id>(
                                 active,
                                 [&](auto& native_output) {
                                   return native_world_.get_active_camera(native_output);
                                 }) == engine::error::success &&
                             active == target.entity;
        snapshot.camera = engine::camera_description{.settings = camera, .is_primary = primary};
      } else if (camera_result != engine::error::not_found) {
        result = camera_result;
      }
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
    applied.push_back(snapshot);

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
    if (result == engine::error::success && has_change(update.changes, structural_change::camera)) {
      result = apply_camera(native_world_, target.entity,
                            source.components.template get<camera_description>());
    }
    if (result == engine::error::success &&
        has_change(update.changes, structural_change::mesh_renderer)) {
      result = apply_mesh_renderer(native_world_, target,
                                   source.components.template get<mesh_renderer_description>() !=
                                       nullptr);
    }
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
    result = native_world_.object_destroy(objects_[removal.old_index].node);
    if (result.failed())
      return result;
  }
  for (std::size_t index = 0; index < staged.size(); ++index) {
    result = native_world_.node_set_identity(staged[index].node, std::move(identities[index]));
    if (result.failed())
      return result;
  }
  members.insert(members.end(),
                 objects_.begin() + static_cast<std::ptrdiff_t>(description_.objects.size()),
                 objects_.end());
  objects_ = std::move(members);
  description_ = std::move(candidate);
  applied.clear();
  staged.clear();
  loader_.collect_unused();
  return engine::error::success;
}

engine::error scene_instance::reload_prefab(std::string_view uri, prefab_asset_ref candidate) {
  std::vector<std::size_t> affected;
  affected.reserve(prefab_instances_.size());
  for (std::size_t index = 0U; index < prefab_instances_.size(); ++index) {
    if (prefab_author(index).prefab->uri == uri) {
      affected.push_back(index);
    }
  }
  if (affected.empty()) {
    return engine::error::not_found;
  }

  auto result = engine::error::success;
  for (const auto index : affected) {
    result = prefab_instances_[index]->validate_reload(candidate,
                                                       prefab_author(index).prefab->overrides);
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
    result = prefab.reload(candidate, prefab_author(index).prefab->overrides);
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

engine::error scene_instance::get_node_info(std::uint64_t index, scene_node_view& out_info,
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
  out_info = {};
  out_info.node = scene_node_id{object.node};
  out_info.parent = scene_node_id{parent};
  out_info.entity = entity_id{entity};
  out_info.uuid = scene_object_identity(native_world_, object.node).uuid;
  out_info.name = scene_object_identity(native_world_, object.node).name;
  out_info.prefab = prefab_origin(object.node);
  if (detail != node_query_detail::identity) {
    auto renderer = native_world_.node_mesh_renderer(object.node);
    if (renderer) {
      out_info.mesh_uri = asset_uri(renderer->mesh);
      out_info.material_uri = asset_uri(renderer->material);
      out_info.component_flags = scene_node_components::mesh_renderer;
    } else if (renderer.error() != error::not_found)
      return renderer.error();
  }
  if (detail == node_query_detail::components) {
    result = native_world_.node_get_local_transform(object.node, out_info.local_transform);
    if (result.failed())
      return result;
    math::camera_settings camera;
    result = native_world_.entity_get_camera(entity_id{entity}, camera);
    if (result.ok()) {
      entity_id active;
      const bool primary = native_world_.get_active_camera(active).ok() && active.get() == entity;
      out_info.camera = camera_description{camera, primary};
      out_info.component_flags |= scene_node_components::camera;
      if (primary)
        out_info.component_flags |= scene_node_components::primary_camera;
    } else if (result != error::not_found)
      return result;
  }
  return engine::error::success;
}

std::optional<prefab_origin_view> scene_instance::prefab_origin(scene_node_id node) const {
  for (std::size_t index = 0; index < prefab_instances_.size(); ++index) {
    const auto& runtime = *prefab_instances_[index];
    const auto& author = prefab_author(index);
    prefab_origin_view origin;
    origin.instance_uuid = author.uuid;
    origin.prefab_uri = author.prefab->uri;
    const auto found = std::ranges::find(runtime.nodes_, node, &prefab_binding::runtime_node::node);
    if (found == runtime.nodes_.end())
      continue;
    origin.flags = runtime.root() == node ? scene_prefab_flags::instance_root
                                          : scene_prefab_flags::source_read_only;
    origin.source_node_uuid = found->address.source_node_uuid;
    if (const auto* source = runtime.find_source_object(origin.source_node_uuid))
      origin.source_local_transform = source->transform;
    for (const auto& value : author.prefab->overrides) {
      if (value.key.source_node_uuid != origin.source_node_uuid ||
          value.key.type_guid != transform_component_type)
        continue;
      if (value.key.property_id == transform_translation_field)
        origin.flags |= scene_prefab_flags::translation_overridden;
      if (value.key.property_id == transform_rotation_field)
        origin.flags |= scene_prefab_flags::rotation_overridden;
      if (value.key.property_id == transform_scale_field)
        origin.flags |= scene_prefab_flags::scale_overridden;
    }
    return origin;
  }
  return std::nullopt;
}

engine::error scene_instances::capture_prefab_header(scene_instance_id instance,
                                                     gneiss::engine::guid uuid,
                                                     object_description& author,
                                                     prefab_asset_ref& source) const noexcept {
  if (!native_world_.is_owner_thread()) {
    return engine::error::invalid_state;
  }
  auto* slot = instances_.get(instance, resource_type::scene_instance);
  if (!slot || !*slot) {
    return engine::error::invalid_handle;
  }
  return (*slot)->capture_prefab_instance_header(uuid, author, source);
}

engine::error scene_instances::capture_prefab_binding(scene_instance_id instance,
                                                      gneiss::engine::guid uuid, std::size_t index,
                                                      mesh_binding& output) const noexcept {
  if (!native_world_.is_owner_thread()) {
    return engine::error::invalid_state;
  }
  auto* slot = instances_.get(instance, resource_type::scene_instance);
  if (!slot || !*slot) {
    return engine::error::invalid_handle;
  }
  return (*slot)->capture_prefab_instance_binding(uuid, index, output);
}

error scene_instances::instantiate_prefab_ready(scene_instance_id instance,
                                                const object_description& author,
                                                prefab_asset_ref prefab,
                                                std::span<const scene_mesh_binding> bindings,
                                                bool replacing, scene_node_id& output) noexcept {
  output = {};
  if (!native_world_.is_owner_thread())
    return error::invalid_state;
  auto* slot = instances_.get(instance, resource_type::scene_instance);
  return !slot || !*slot ? error::invalid_handle
                         : (*slot)->instantiate_prefab_ready(author, std::move(prefab), bindings,
                                                             replacing, output);
}
error scene_instances::remove_prefab_instance(scene_instance_id instance,
                                              gneiss::engine::guid uuid) noexcept {
  if (!native_world_.is_owner_thread())
    return error::invalid_state;
  auto* slot = instances_.get(instance, resource_type::scene_instance);
  return !slot || !*slot ? error::invalid_handle : (*slot)->remove_prefab_instance(uuid);
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
      auto pending = prefab_author(index).prefab->overrides;
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
      prefab_author(index).prefab->overrides.swap(pending);
      return engine::error::success;
    }
  }
  return engine::error::invalid_handle;
}

engine::error scene_instance::create_node(const node_creation& desc, scene_node_id* out_node) {
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
    description_.objects.reserve(description_.objects.size() + 1U);
    staged_object target{.uuid = author.uuid,
                         .entity = UINT64_C(0),
                         .node = null_scene_node_id,
                         .mesh = {},
                         .material = {}};
    const auto result = commit_object(native_world_, author, target, desc.parent);
    if (result != engine::error::success) {
      if (target.node) {
        (void)native_world_.object_destroy(target.node);
      }
      return result;
    }
    *out_node = target.node;
    objects_.insert(objects_.begin() + static_cast<std::ptrdiff_t>(description_.objects.size()),
                    {target.node});
    description_.objects.push_back(std::move(author));
    return engine::error::success;
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
  try {
    std::string author_name(name);
    const auto index = static_cast<std::size_t>(std::distance(objects_.begin(), found));
    auto changed =
        native_world_.node_set_identity(node, {description_.objects[index].uuid, author_name});
    if (changed.failed())
      return changed;
    description_.objects[index].name.swap(author_name);
    return engine::error::success;
  } catch (const std::bad_alloc&) {
    return engine::error::out_of_memory;
  }
}

engine::error scene_instance::reparent_node(scene_node_id node, scene_node_id parent) {
  const auto found = find_authored_node(node);
  if (found == objects_.end()) {
    return engine::error::invalid_handle;
  }
  std::optional<gneiss::engine::guid> parent_uuid;
  try {
    if (parent != null_scene_node_id) {
      const auto parent_object = find_authored_node(parent);
      if (parent_object == objects_.end()) {
        return engine::error::invalid_handle;
      }
      parent_uuid = scene_object_identity(native_world_, parent_object->node).uuid;
    }
    const auto result =
        native_world_.node_reparent(gneiss::scene_node_id{node}, gneiss::scene_node_id{parent});
    if (result != engine::error::success) {
      return result;
    }
    const auto index = static_cast<std::size_t>(std::distance(objects_.begin(), found));
    description_.objects[index].parent_uuid.swap(parent_uuid);
    return engine::error::success;
  } catch (const std::bad_alloc&) {
    return engine::error::out_of_memory;
  }
}

engine::error scene_instance::capture_subtree(scene_node_id root,
                                              scene_subtree_snapshot& output) const {
  if (!native_world_.is_owner_thread())
    return error::invalid_state;
  const auto root_member = std::ranges::find(objects_, root, &object::node);
  const auto index = static_cast<std::size_t>(root_member - objects_.begin());
  if (index >= description_.objects.size())
    return error::invalid_handle;
  scene_subtree_snapshot snapshot;
  auto status = capture_description(snapshot.description);
  if (status.failed())
    return status;
  snapshot.root_uuid = snapshot.description.objects[index].uuid;
  snapshot.parent_uuid = snapshot.description.objects[index].parent_uuid.value_or(guid{});
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
        status = capture_prefab_instance_binding(author.uuid, i, resources.source_nodes[i]);
        if (status.failed())
          return status;
      }
    } else if (author.components.template get<mesh_renderer_description>()) {
      const auto renderer = native_world_.node_mesh_renderer(find_node(author.uuid));
      if (!renderer)
        return renderer.error();
      snapshot.resources[author.uuid].renderer = {renderer->mesh, renderer->material};
    }
  }
  output = std::move(snapshot);
  return error::success;
}

engine::error scene_instance::restore_subtree(const scene_subtree_snapshot& snapshot,
                                              scene_node_id parent,
                                              std::span<const uuid_mapping> mappings,
                                              scene_node_id* output) {
  *output = {};
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
  staged.description_.uuid = description_.uuid;
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
      scene_node_id created;
      status = staged.instantiate_prefab_ready(author, asset->second.prefab,
                                               asset->second.source_nodes, false, created);
    } else {
      scene_mesh_binding resources;
      if (author.components.template get<mesh_renderer_description>()) {
        if (asset == snapshot.resources.end())
          return error::not_found;
        resources = asset->second.renderer;
        if (asset_uri(resources.mesh) !=
                author.components.template get<mesh_renderer_description>()->mesh_uri ||
            asset_uri(resources.material) !=
                author.components.template get<mesh_renderer_description>()->material_uri)
          return error::not_found;
        if (!loader_.owns(resources.mesh) || !loader_.owns(resources.material))
          return error::invalid_handle;
      }
      // 先扩容，保证创建成功后登记成员不会因分配失败遗漏节点。
      staged.objects_.reserve(staged.objects_.size() + 1);
      staged.description_.objects.reserve(staged.description_.objects.size() + 1);
      auto created = create_scene_object(native_world_, author, node_parent,
                                         {resources.mesh, resources.material});
      if (!created)
        return created.error();
      staged.objects_.insert(staged.objects_.begin() +
                                 static_cast<std::ptrdiff_t>(staged.description_.objects.size()),
                             {*created});
      staged.description_.objects.push_back(std::move(author));
      status = error::success;
    }
    if (status.failed())
      return status;
  }
  const auto created_root = staged.find_node(remap.at(snapshot.root_uuid));
  auto& root_author = *std::ranges::find(staged.description_.objects, remap.at(snapshot.root_uuid),
                                         &object_description::uuid);
  root_author.parent_uuid =
      parent
          ? std::optional<gneiss::engine::guid>(scene_object_identity(native_world_, parent).uuid)
          : std::nullopt;
  // 合并前完成分配，提交后临时集合放弃节点所有权。
  objects_.reserve(objects_.size() + staged.objects_.size());
  description_.objects.reserve(description_.objects.size() + staged.description_.objects.size());
  prefab_instances_.reserve(prefab_instances_.size() + staged.prefab_instances_.size());
  status = native_world_.node_reparent(created_root, parent);
  if (status.failed())
    return status;
  const auto explicit_count = staged.description_.objects.size();
  objects_.insert(objects_.begin() + static_cast<std::ptrdiff_t>(description_.objects.size()),
                  staged.objects_.begin(),
                  staged.objects_.begin() + static_cast<std::ptrdiff_t>(explicit_count));
  objects_.insert(objects_.end(),
                  staged.objects_.begin() + static_cast<std::ptrdiff_t>(explicit_count),
                  staged.objects_.end());
  for (auto& author : staged.description_.objects)
    description_.objects.push_back(std::move(author));
  for (auto& binding : staged.prefab_instances_)
    prefab_instances_.push_back(std::move(binding));
  staged.objects_.clear();
  staged.prefab_instances_.clear();
  camera.committed = true;
  *output = created_root;
  return error::success;
}

engine::error scene_instance::destroy_subtree(scene_node_id root) {
  const auto member = std::ranges::find(objects_, root, &object::node);
  if (member == objects_.end())
    return error::invalid_handle;
  const auto origin = prefab_origin(root);
  if (origin && has_flags(origin->flags, scene_prefab_flags::source_read_only))
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
    // 先采集源描述索引，销毁后不再通过节点查询身份。
    std::vector<std::size_t> ordinary;
    for (std::size_t index = 0; index < description_.objects.size(); ++index)
      if (contains(objects_[index].node))
        ordinary.push_back(index);
    std::vector<std::size_t> prefabs;
    for (std::size_t index = 0; index < prefab_instances_.size(); ++index)
      if (contains(prefab_instances_[index]->root()))
        prefabs.push_back(index);
    const auto status = native_world_.object_destroy(root);
    if (status.failed())
      return status;
    std::erase_if(objects_, [&](const auto& object) { return contains(object.node); });
    for (auto it = ordinary.rbegin(); it != ordinary.rend(); ++it)
      description_.objects.erase(description_.objects.begin() + static_cast<std::ptrdiff_t>(*it));
    for (auto it = prefabs.rbegin(); it != prefabs.rend(); ++it) {
      prefab_instances_.erase(prefab_instances_.begin() + static_cast<std::ptrdiff_t>(*it));
    }
    loader_.collect_unused();
    return error::success;
  } catch (const std::bad_alloc&) {
    return error::out_of_memory;
  }
}

engine::error scene_instance::create_mesh_renderer_node(const mesh_renderer_node_creation& desc,
                                                        scene_node_id* out_node,
                                                        const scene_mesh_binding* prepared) {
  if (prepared && (engine::asset_kind(prepared->mesh) != engine::asset_type::mesh ||
                   engine::asset_kind(prepared->material) != engine::asset_type::material ||
                   !loader_.owns(prepared->mesh) || !loader_.owns(prepared->material) ||
                   engine::asset_uri(prepared->mesh) != desc.mesh_uri ||
                   engine::asset_uri(prepared->material) != desc.material_uri)) {
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
    description_.objects.reserve(description_.objects.size() + 1U);
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
    result = commit_object(native_world_, author, target, desc.parent);
    if (result != engine::error::success) {
      if (target.node) {
        (void)native_world_.object_destroy(target.node);
      }
      return result;
    }
    *out_node = target.node;
    objects_.insert(objects_.begin() + static_cast<std::ptrdiff_t>(description_.objects.size()),
                    {target.node});
    description_.objects.push_back(std::move(author));
    return engine::error::success;
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
    engine::mesh_renderer_description author{.mesh_uri = std::string(mesh_uri),
                                             .material_uri = std::string(material_uri)};
    auto current = native_world_.node_mesh_renderer(node);
    if (!current && current.error() != error::not_found)
      return current.error();
    const auto binding = current ? *current : mesh_renderer{};
    // 编辑资产选择不等于重载；未改变的 URI 继续使用节点选定的版本。
    auto mesh = engine::asset_uri(binding.mesh) == mesh_uri ? binding.mesh : engine::mesh{};
    auto material =
        engine::asset_uri(binding.material) == material_uri ? binding.material : engine::material{};

    auto result = engine::error::success;
    if (!mesh) {
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
    if (result == engine::error::success && !material) {
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
    const auto index = static_cast<std::size_t>(std::distance(objects_.begin(), found));
    auto pending = description_.objects[index].components;
    pending.set(std::move(author));
    const engine::mesh_renderer renderer{.mesh = mesh, .material = material};
    result = native_world_.entity_set_mesh_renderer(
        gneiss::entity_id{scene_object_entity(native_world_, found->node)}, renderer);
    if (result != engine::error::success) {
      return result;
    }
    description_.objects[index].components = std::move(pending);
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
        return error::not_found;
      entity_id active;
      auto status = native_world_.get_active_camera(active);
      if (status.failed() && status != error::not_ready)
        return status;
      const auto entity = entity_id{scene_object_entity(native_world_, node)};
      if (desc.is_primary != (active == entity))
        return error::unsupported;
      auto pending = prefab_author(index).prefab->overrides;
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
      prefab_author(index).prefab->overrides.swap(pending);
      return error::success;
    }
  }
  const auto found = find_ordinary_node(node);
  if (found == objects_.end()) {
    return engine::error::invalid_handle;
  }
  auto pending = description_.objects;
  const auto index = static_cast<std::size_t>(std::distance(objects_.begin(), found));
  if (desc.is_primary) {
    for (auto& author : pending) {
      if (author.components.template get<camera_description>()) {
        author.components.update<camera_description>(
            [&](auto& value) { value.is_primary = false; });
      }
    }
  }
  pending[index].components.set(desc);
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
  description_.objects = std::move(pending);
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
  const auto index = static_cast<std::size_t>(std::distance(objects_.begin(), found));
  const auto result = native_world_.entity_remove_camera(
      gneiss::entity_id{scene_object_entity(native_world_, found->node)});
  if (result == engine::error::success) {
    description_.objects[index].components.erase<camera_description>();
  }
  return result;
}

engine::error scene_instance::remove_mesh_renderer(scene_node_id node) {
  const auto found = find_ordinary_node(node);
  if (found == objects_.end()) {
    return engine::error::invalid_handle;
  }
  const auto index = static_cast<std::size_t>(std::distance(objects_.begin(), found));
  const auto result = native_world_.entity_remove_mesh_renderer(
      gneiss::entity_id{scene_object_entity(native_world_, found->node)});
  if (result != engine::error::success) {
    return result;
  }
  description_.objects[index].components.erase<mesh_renderer_description>();
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

engine::error scene_instance::capture_description(scene_description& output) const {
  auto current = description_;
  std::uint64_t active_camera = UINT64_C(0);
  const auto active_result =
      engine::write_identifier<entity_id>(active_camera, [&](auto& native_output) {
        return native_world_.get_active_camera(native_output);
      });
  if (active_result != engine::error::success && active_result != engine::error::not_ready) {
    return active_result;
  }
  for (std::size_t index = 0; index < description_.objects.size(); ++index) {
    const auto& runtime = objects_[index];
    auto& author = current.objects[index];
    auto identity = native_world_.node_identity(runtime.node);
    if (!identity)
      return identity.error();
    author.uuid = identity->uuid;
    author.name = identity->name;
    scene_node_id parent;
    auto parent_status = native_world_.node_get_parent(runtime.node, parent);
    if (parent_status.failed())
      return parent_status;
    author.parent_uuid.reset();
    if (parent) {
      auto parent_identity = native_world_.node_identity(parent);
      if (!parent_identity)
        return parent_identity.error();
      author.parent_uuid = parent_identity->uuid;
    }
    if (author.prefab) {
      math::transform transform;
      const auto status = native_world_.node_get_local_transform(runtime.node, transform);
      if (status.failed())
        return status;
      author.transform = transform;
      continue;
    }
    auto renderer = native_world_.node_mesh_renderer(runtime.node);
    if (renderer)
      author.components.set(mesh_renderer_description{std::string(asset_uri(renderer->mesh)),
                                                      std::string(asset_uri(renderer->material))});
    else if (renderer.error() == error::not_found)
      author.components.erase<mesh_renderer_description>();
    else
      return renderer.error();
    math::transform transform = {};
    auto result = native_world_.entity_get_local_transform(
        gneiss::entity_id{scene_object_entity(native_world_, runtime.node)}, transform);
    if (result != engine::error::success) {
      return result;
    }
    author.transform = transform;
    math::camera_settings camera;
    result = native_world_.entity_get_camera(
        entity_id{scene_object_entity(native_world_, runtime.node)}, camera);
    if (result.ok()) {
      author.components.set(camera_description{
          camera, scene_object_entity(native_world_, runtime.node) == active_camera});
    } else if (result == error::not_found)
      author.components.erase<camera_description>();
    else
      return result;
  }
  output = std::move(current);
  return error::success;
}

engine::error scene_instance::serialize(std::string& out_json) const {
  scene_description current;
  const auto status = capture_description(current);
  if (status.failed())
    return status;
  auto decoded_result = serialize_scene_description(current);
  if (!decoded_result) {
    return decoded_result.error();
  }
  out_json = std::move(*decoded_result);
  return gneiss::engine::error::success;
}

scene_instances::scene_instances(world& native_world, resource_service& loader) noexcept
    : native_world_(native_world), loader_(loader), domain_(allocate_domain()),
      instances_(domain_) {}

scene_instances::~scene_instances() noexcept = default;

engine::error scene_instances::reload_loaded(scene_instance_id instance,
                                             scene_description description) noexcept try {
  auto* value = instances_.get(instance, resource_type::scene_instance);
  return value && *value ? (*value)->reload(std::move(description)) : error::invalid_handle;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}

engine::error scene_instances::reload_prefab_loaded(scene_instance_id instance,
                                                    std::string_view uri,
                                                    prefab_asset_ref candidate) noexcept try {
  if (!candidate)
    return error::invalid_argument;
  auto* value = instances_.get(instance, resource_type::scene_instance);
  return value && *value ? (*value)->reload_prefab(uri, std::move(candidate))
                         : error::invalid_handle;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}

engine::error scene_instances::create_empty(gneiss::engine::guid scene_uuid,
                                            scene_instance_id* out_instance) noexcept {
  if (out_instance == nullptr || !is_valid()) {
    return engine::error::invalid_argument;
  }
  *out_instance = {};
  try {
    const std::string json = std::string{"{\"format\":\"gneiss.scene\",\"version\":5,"} +
                             "\"scene_uuid\":\"" + scene_uuid.string() + "\",\"objects\":[]}";
    auto parsed = engine::parse_scene_description(json);
    if (!parsed)
      return parsed.error().error;
    auto description = std::move(*parsed);
    auto instance = std::make_unique<scene_instance>(native_world_, loader_);
    instance->initialize_staged(std::move(description));
    auto created = instances_.create(resource_type::scene_instance, std::move(instance));
    if (!created)
      return created.error();
    *out_instance = *created;
    return error::success;
  } catch (const std::bad_alloc&) {
    return engine::error::out_of_memory;
  } catch (...) {
    return engine::error::internal;
  }
}

engine::error scene_instances::capture_description(scene_instance_id instance,
                                                   scene_description& output) const noexcept {
  try {
    const auto* value = instances_.get(instance, resource_type::scene_instance);
    if (value == nullptr || *value == nullptr) {
      return engine::error::invalid_handle;
    }
    return (*value)->capture_description(output);
  } catch (const std::bad_alloc&) {
    return engine::error::out_of_memory;
  } catch (...) {
    return engine::error::internal;
  }
}

engine::error scene_instances::serialize(scene_instance_id instance,
                                         std::string& out_json) const noexcept {
  try {
    const auto* value = instances_.get(instance, resource_type::scene_instance);
    if (value == nullptr || *value == nullptr) {
      return engine::error::invalid_handle;
    }
    return (*value)->serialize(out_json);
  } catch (const std::bad_alloc&) {
    return engine::error::out_of_memory;
  } catch (...) {
    return engine::error::internal;
  }
}

engine::error scene_instances::unload(scene_instance_id instance) noexcept {
  return instances_.destroy(instance, resource_type::scene_instance);
}

engine::error scene_instances::find_node(scene_instance_id instance, gneiss::engine::guid uuid,
                                         scene_node_id& out_node_value) const noexcept {
  auto out_node_storage = out_node_value;
  auto* out_node = &out_node_storage;
  const auto operation_result = [&]() -> engine::error {
    if (out_node == nullptr) {
      return engine::error::invalid_argument;
    }
    *out_node = null_scene_node_id;
    const auto* value = instances_.get(instance, resource_type::scene_instance);
    if (value == nullptr || *value == nullptr) {
      return engine::error::invalid_handle;
    }
    *out_node = (*value)->find_node(uuid);
    return *out_node == null_scene_node_id ? engine::error::not_found : engine::error::success;
  }();
  out_node_value = scene_node_id{out_node_storage};
  return operation_result;
}

engine::error scene_instances::get_node_count(scene_instance_id instance,
                                              std::uint64_t* out_count) const noexcept {
  if (out_count == nullptr) {
    return engine::error::invalid_argument;
  }
  *out_count = 0U;
  const auto* value = instances_.get(instance, resource_type::scene_instance);
  if (value == nullptr || *value == nullptr) {
    return engine::error::invalid_handle;
  }
  *out_count = (*value)->node_count();
  return engine::error::success;
}

engine::error
scene_instances::set_prefab_source_transform(scene_instance_id instance, scene_node_id node_value,
                                             const math::transform& transform) noexcept {
  auto node = node_value;
  try {
    auto* value = instances_.get(instance, resource_type::scene_instance);
    return value == nullptr || *value == nullptr
               ? engine::error::invalid_handle
               : (*value)->set_prefab_source_transform(node, transform);
  } catch (const std::bad_alloc&) {
    return engine::error::out_of_memory;
  } catch (...) {
    return engine::error::internal;
  }
}

engine::error scene_instances::get_node_info(scene_instance_id instance, std::uint64_t index,
                                             scene_node_view& out_info,
                                             node_query_detail detail) const noexcept {
  out_info = {};
  const auto* value = instances_.get(instance, resource_type::scene_instance);
  if (value == nullptr || *value == nullptr) {
    return engine::error::invalid_handle;
  }
  return (*value)->get_node_info(index, out_info, detail);
}

engine::error scene_instances::create_node(scene_instance_id instance, const node_creation& desc,
                                           scene_node_id& out_node_value) noexcept {
  auto out_node_storage = out_node_value;
  auto* out_node = &out_node_storage;
  const auto operation_result = [&]() -> engine::error {
    if (out_node == nullptr) {
      return engine::error::invalid_argument;
    }
    *out_node = null_scene_node_id;
    try {
      auto* value = instances_.get(instance, resource_type::scene_instance);
      return value == nullptr || *value == nullptr ? engine::error::invalid_handle
                                                   : (*value)->create_node(desc, out_node);
    } catch (...) {
      return engine::error::internal;
    }
  }();
  out_node_value = scene_node_id{out_node_storage};
  return operation_result;
}

engine::error scene_instances::set_node_name(scene_instance_id instance, scene_node_id node_value,
                                             std::string_view name) noexcept {
  auto node = node_value;
  try {
    auto* value = instances_.get(instance, resource_type::scene_instance);
    return value == nullptr || *value == nullptr ? engine::error::invalid_handle
                                                 : (*value)->set_node_name(node, name);
  } catch (...) {
    return engine::error::internal;
  }
}

engine::error scene_instances::reparent_node(scene_instance_id instance, scene_node_id node_value,
                                             scene_node_id parent_value) noexcept {
  auto node = node_value;
  auto parent = parent_value;
  try {
    auto* value = instances_.get(instance, resource_type::scene_instance);
    return value == nullptr || *value == nullptr ? engine::error::invalid_handle
                                                 : (*value)->reparent_node(node, parent);
  } catch (...) {
    return engine::error::internal;
  }
}

engine::error
scene_instances::capture_subtree(scene_instance_id instance, scene_node_id root_value,
                                 scene_subtree_snapshot& out_snapshot) const noexcept {
  auto root = root_value;
  try {
    const auto* value = instances_.get(instance, resource_type::scene_instance);
    return value == nullptr || *value == nullptr ? engine::error::invalid_handle
                                                 : (*value)->capture_subtree(root, out_snapshot);
  } catch (...) {
    return engine::error::internal;
  }
}

engine::error scene_instances::restore_subtree(scene_instance_id instance,
                                               const scene_subtree_snapshot& snapshot,
                                               scene_node_id parent_value,
                                               std::span<const uuid_mapping> mappings,
                                               scene_node_id& out_root_value) noexcept {
  auto parent = parent_value;
  auto out_root_storage = out_root_value;
  auto* out_root = &out_root_storage;
  const auto operation_result = [&]() -> engine::error {
    if (out_root == nullptr) {
      return engine::error::invalid_argument;
    }
    *out_root = null_scene_node_id;
    try {
      auto* value = instances_.get(instance, resource_type::scene_instance);
      return value == nullptr || *value == nullptr
                 ? engine::error::invalid_handle
                 : (*value)->restore_subtree(snapshot, parent, mappings, out_root);
    } catch (...) {
      return engine::error::internal;
    }
  }();
  out_root_value = scene_node_id{out_root_storage};
  return operation_result;
}

engine::error scene_instances::destroy_subtree(scene_instance_id instance,
                                               scene_node_id root_value) noexcept {
  auto root = root_value;
  try {
    auto* value = instances_.get(instance, resource_type::scene_instance);
    return value == nullptr || *value == nullptr ? engine::error::invalid_handle
                                                 : (*value)->destroy_subtree(root);
  } catch (...) {
    return engine::error::internal;
  }
}

engine::error scene_instances::create_mesh_renderer_node(scene_instance_id instance,
                                                         const mesh_renderer_node_creation& desc,
                                                         scene_node_id& out_node_value) noexcept {
  auto out_node_storage = out_node_value;
  auto* out_node = &out_node_storage;
  const auto operation_result = [&]() -> engine::error {
    if (out_node == nullptr) {
      return engine::error::invalid_argument;
    }
    *out_node = null_scene_node_id;
    try {
      auto* value = instances_.get(instance, resource_type::scene_instance);
      return value == nullptr || *value == nullptr
                 ? engine::error::invalid_handle
                 : (*value)->create_mesh_renderer_node(desc, out_node);
    } catch (...) {
      return engine::error::internal;
    }
  }();
  out_node_value = scene_node_id{out_node_storage};
  return operation_result;
}

engine::error scene_instances::create_mesh_renderer_node_ready(
    scene_instance_id instance, const mesh_renderer_node_creation& desc,
    const mesh_binding& prepared, scene_node_id& output_value) noexcept try {
  auto output = output_value;
  const auto operation_result = [&]() -> engine::error {
    output = null_scene_node_id;
    const auto owner = native_world_.replace_mesh_renderers({});
    if (owner != engine::error::success) {
      return owner;
    }
    auto* value = instances_.get(instance, resource_type::scene_instance);
    return value == nullptr || *value == nullptr
               ? engine::error::invalid_handle
               : (*value)->create_mesh_renderer_node(desc, &output, &prepared);
  }();
  output_value = scene_node_id{output};
  return operation_result;
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}

engine::error scene_instances::capture_mesh_binding(scene_instance_id instance, scene_node_id node,
                                                    mesh_binding& output) const noexcept {
  output = {};
  const auto owner = native_world_.replace_mesh_renderers({});
  if (owner != engine::error::success) {
    return owner;
  }
  const auto* value = instances_.get(instance, resource_type::scene_instance);
  return value == nullptr || *value == nullptr ? engine::error::invalid_handle
                                               : (*value)->capture_mesh_binding(node, output);
}

engine::error scene_instances::replace_mesh_binding(scene_instance_id instance, scene_node_id node,
                                                    const mesh_binding& expected,
                                                    const mesh_binding& replacement) noexcept {
  const auto owner = native_world_.replace_mesh_renderers({});
  if (owner != engine::error::success) {
    return owner;
  }
  using type = engine::asset_type;
  if (engine::asset_kind(replacement.mesh) != type::mesh ||
      engine::asset_kind(replacement.material) != type::material ||
      !loader_.owns(replacement.mesh) || !loader_.owns(replacement.material)) {
    return engine::error::invalid_argument;
  }
  auto* value = instances_.get(instance, resource_type::scene_instance);
  return value == nullptr || *value == nullptr
             ? engine::error::invalid_handle
             : (*value)->replace_mesh_binding(node, expected, replacement);
}

engine::error scene_instances::set_mesh_renderer(scene_instance_id instance,
                                                 scene_node_id node_value,
                                                 std::string_view mesh_uri,
                                                 std::string_view material_uri) noexcept {
  auto node = node_value;
  try {
    auto* value = instances_.get(instance, resource_type::scene_instance);
    return value == nullptr || *value == nullptr
               ? engine::error::invalid_handle
               : (*value)->set_mesh_renderer(node, mesh_uri, material_uri);
  } catch (...) {
    return engine::error::internal;
  }
}

engine::error scene_instances::validate_editable_node(scene_instance_id instance,
                                                      scene_node_id node_value) const noexcept {
  auto node = node_value;
  const auto* value = instances_.get(instance, resource_type::scene_instance);
  if (value == nullptr || *value == nullptr) {
    return engine::error::invalid_handle;
  }
  return (*value)->contains_editable_node(scene_node_id{node}) ? engine::error::success
                                                               : engine::error::invalid_handle;
}

engine::error scene_instances::set_camera(scene_instance_id instance, scene_node_id node_value,
                                          const engine::camera_description& desc) noexcept {
  auto node = node_value;
  auto* value = instances_.get(instance, resource_type::scene_instance);
  return value == nullptr || *value == nullptr ? engine::error::invalid_handle
                                               : (*value)->set_camera(node, desc);
}

engine::error scene_instances::remove_camera(scene_instance_id instance,
                                             scene_node_id node_value) noexcept {
  auto node = node_value;
  auto* value = instances_.get(instance, resource_type::scene_instance);
  return value == nullptr || *value == nullptr ? engine::error::invalid_handle
                                               : (*value)->remove_camera(node);
}

engine::error scene_instances::remove_mesh_renderer(scene_instance_id instance,
                                                    scene_node_id node_value) noexcept {
  auto node = node_value;
  auto* value = instances_.get(instance, resource_type::scene_instance);
  return value == nullptr || *value == nullptr ? engine::error::invalid_handle
                                               : (*value)->remove_mesh_renderer(node);
}

engine::error scene_instances::destroy_node(scene_instance_id instance,
                                            scene_node_id node_value) noexcept {
  auto node = node_value;
  try {
    auto* value = instances_.get(instance, resource_type::scene_instance);
    return value == nullptr || *value == nullptr ? engine::error::invalid_handle
                                                 : (*value)->destroy_node(node);
  } catch (...) {
    return engine::error::internal;
  }
}

} // namespace gneiss::engine
