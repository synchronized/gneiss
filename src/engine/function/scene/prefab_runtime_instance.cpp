// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/scene/prefab_runtime_instance.hpp"
#include "engine/core/error.hpp"
#include "engine/core/scene_types.hpp"
#include "engine/function/scene/prefab_property_override.hpp"

#include "engine/core/identifier_conversion.hpp"
#include "engine/core/meta/registration.hpp"
#include "engine/function/world/component_identity.hpp"

#include "engine/function/world/world.hpp"
#include "engine/function/world/world_reflection.hpp"

#include "engine/function/scene/structural_diff.hpp"

#include <algorithm>
#include <new>
#include <type_traits>
#include <unordered_map>

namespace {

// 必须先于候选实例构造，保证候选释放后再恢复原相机；异常路径同样适用。
struct camera_restore_guard {
  gneiss::engine::world& native_world;
  std::uint64_t previous;
  bool committed{};
  ~camera_restore_guard() {
    if (!committed && previous != UINT64_C(0)) {
      (void)native_world.set_active_camera(gneiss::entity_id{previous});
    }
  }
};

gneiss::engine::math::transform
to_transform(const gneiss::engine::object_description& object) noexcept {
  gneiss::engine::math::transform value{};
  value.translation = object.translation;
  value.rotation = object.rotation;
  value.scale = object.scale;
  return value;
}

gneiss::engine::error
apply_camera(gneiss::engine::world& native_world, std::uint64_t entity,
             const std::optional<gneiss::engine::camera_description>& camera) noexcept {
  if (!camera) {
    const auto result = native_world.entity_remove_camera(gneiss::entity_id{entity});
    return result == gneiss::engine::error::not_found ? gneiss::engine::error::success : result;
  }
  const gneiss::engine::math::camera_settings value{camera->vertical_field_of_view_radians,
                                                    camera->near_plane, camera->far_plane};
  return native_world.entity_set_camera(gneiss::entity_id{entity}, value, camera->is_primary);
}

} // namespace

namespace gneiss::engine {

prefab_runtime_instance::prefab_runtime_instance(world& native_world, resource_service& loader,
                                                 prefab_asset_ref prefab,
                                                 std::string instance_uuid) noexcept
    : native_world_(native_world), loader_(loader), prefab_(std::move(prefab)),
      instance_uuid_(std::move(instance_uuid)) {}

engine::error
prefab_runtime_instance::validate_reload(const prefab_asset_ref& candidate,
                                         const meta::registry_ref& registry,
                                         const std::vector<prefab_property_override>& overrides) {
  if (!candidate || !prefab_ || candidate.get()->uuid != prefab_.get()->uuid ||
      candidate.get()->objects.empty()) {
    return engine::error::invalid_argument;
  }
  const auto root = std::ranges::find_if(candidate->objects,
                                         [](const auto& value) { return !value.parent_uuid; });
  if (root == candidate->objects.end() ||
      std::ranges::count_if(candidate->objects,
                            [](const auto& value) { return !value.parent_uuid; }) != 1 ||
      find_node(root->uuid) != root_node_)
    return error::unsupported;
  structural_diff diff;
  auto result = build_structural_diff(prefab_.get()->objects, candidate.get()->objects, diff);
  if (result != engine::error::success) {
    return result;
  }

  std::vector<engine::mesh> meshes(candidate.get()->objects.size());
  std::vector<engine::material> materials(candidate.get()->objects.size());
  for (std::size_t index = 0U; index < candidate.get()->objects.size(); ++index) {
    const auto& source = candidate.get()->objects[index];
    if (!source.mesh_renderer) {
      continue;
    }
    result = [&] {
      render_asset_ref cached;
      auto status = engine::error::success;
      {
        auto loaded = loader_.find({source.mesh_renderer->mesh_uri, asset_type::mesh});
        if (loaded)
          cached = std::move(*loaded);
        else {
          cached = {};
          status = loaded.error();
        }
      };
      if (status.ok())
        meshes[index] = asset_cast<mesh_resource>(cached);
      return status;
    }();
    if (result == engine::error::success) {
      result = [&] {
        render_asset_ref cached;
        auto status = engine::error::success;
        {
          auto loaded = loader_.find({source.mesh_renderer->material_uri, asset_type::material});
          if (loaded)
            cached = std::move(*loaded);
          else {
            cached = {};
            status = loaded.error();
          }
        };
        if (status.ok())
          materials[index] = asset_cast<material_resource>(cached);
        return status;
      }();
    }
    if (result != engine::error::success) {
      return result;
    }
  }

  const auto camera_type = engine::camera_component_type;
  for (const auto& override_value : overrides) {
    if (override_value.key.node.instance_uuid != instance_uuid_) {
      return engine::error::invalid_argument;
    }
    result = validate_prefab_property_override(registry, override_value);
    const auto source =
        std::ranges::find(candidate.get()->objects, override_value.key.node.source_node_uuid,
                          &object_description::uuid);
    if (result != engine::error::success || source == candidate.get()->objects.end()) {
      return result == engine::error::success ? engine::error::not_found : result;
    }
    if (!source->parent_uuid && override_value.key.type_id == transform_component_type)
      return error::invalid_argument;
    if (override_value.key.type_id == camera_type && !source->camera) {
      return engine::error::invalid_state;
    }
  }
  return engine::error::success;
}

engine::error
prefab_runtime_instance::reload(prefab_asset_ref candidate, const meta::registry_ref& registry,
                                const std::vector<prefab_property_override>& overrides) {

  auto result = validate_reload(candidate, registry, overrides);
  if (result != engine::error::success) {
    return result;
  }
  structural_diff diff;

  result = build_structural_diff(prefab_.get()->objects, candidate.get()->objects, diff);
  if (result != engine::error::success) {
    return result;
  }

  struct staged_node {
    prefab_author_address address;
    std::string name;
    std::uint64_t entity{};
    scene_node_id node{};
    engine::mesh mesh;
    engine::material material;
  };
  std::vector<staged_node> staged;
  staged.reserve(candidate.get()->objects.size());
  std::unordered_map<std::string_view, std::size_t> old_indices;
  old_indices.reserve(nodes_.size());
  for (std::size_t index = 0U; index < nodes_.size(); ++index) {
    old_indices.emplace(nodes_[index].address.source_node_uuid, index);
  }
  for (const auto& source : candidate.get()->objects) {
    staged_node target;
    target.address = {instance_uuid_, source.uuid};
    target.name = source.name;
    if (const auto previous = old_indices.find(source.uuid); previous != old_indices.end()) {
      target.entity = scene_object_entity(native_world_, nodes_[previous->second].node);
      target.node = nodes_[previous->second].node;
    }
    if (source.mesh_renderer) {
      result = [&] {
        render_asset_ref cached;
        auto status = engine::error::success;
        {
          auto loaded = loader_.find({source.mesh_renderer->mesh_uri, asset_type::mesh});
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
            auto loaded = loader_.find({source.mesh_renderer->material_uri, asset_type::material});
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

  std::vector<runtime_node> members;
  members.reserve(staged.size());
  std::vector<scene_node_identity> identities;
  identities.reserve(staged.size());
  for (const auto& value : staged) {
    members.push_back({value.address, value.node, true});
    identities.push_back({value.address.source_node_uuid, value.name});
  }
  std::unordered_map<std::string_view, scene_node_id> runtime_nodes;
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
    const auto& source = candidate.get()->objects[addition.new_index];
    auto& target = staged[addition.new_index];
    const auto parent = source.parent_uuid ? runtime_nodes.at(*source.parent_uuid) : root_node_;
    auto created =
        create_scene_object(native_world_, source, parent, {target.mesh, target.material});
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
        (void)apply_camera(native_world_, target.entity, snapshot.camera);
      }
      if (has_change(snapshot.changes, structural_change::mesh_renderer)) {
        const auto& old_source = prefab_.get()->objects[snapshot.old_index];
        if (old_source.mesh_renderer) {
          (void)native_world_.entity_set_mesh_renderer(gneiss::entity_id{target.entity},
                                                       snapshot.renderer);
        } else {
          (void)native_world_.entity_remove_mesh_renderer(gneiss::entity_id{target.entity});
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
        snapshot.camera = engine::camera_description{
            .vertical_field_of_view_radians = camera.fov,
            .near_plane = camera.near,
            .far_plane = camera.far,
            .is_primary = engine::write_identifier<entity_id>(
                              active,
                              [&](auto& native_output) {
                                return native_world_.get_active_camera(native_output);
                              }) == engine::error::success &&
                          active == target.entity};
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
    const auto& source = candidate.get()->objects[update.new_index];
    if (target.node != root_node_ && has_change(update.changes, structural_change::parent)) {
      const auto parent = source.parent_uuid ? runtime_nodes.at(*source.parent_uuid) : root_node_;
      result = native_world_.node_reparent(gneiss::scene_node_id{target.node},
                                           gneiss::scene_node_id{parent});
    }
    if (result == engine::error::success && target.node != root_node_ &&
        has_change(update.changes, structural_change::transform)) {
      const auto transform = to_transform(source);
      result =
          native_world_.node_set_local_transform(gneiss::scene_node_id{target.node}, transform);
    }
    if (result == engine::error::success && has_change(update.changes, structural_change::camera)) {
      result = apply_camera(native_world_, target.entity, source.camera);
    }
    if (result == engine::error::success &&
        has_change(update.changes, structural_change::mesh_renderer)) {
      if (source.mesh_renderer) {
        const mesh_renderer renderer{.mesh = target.mesh, .material = target.material};
        result = native_world_.entity_set_mesh_renderer(gneiss::entity_id{target.entity}, renderer);
      } else {
        result = native_world_.entity_remove_mesh_renderer(gneiss::entity_id{target.entity});
      }
    }
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
  result = apply_overrides(registry, overrides);
  if (result != engine::error::success) {
    nodes_ = std::move(previous_nodes);
    rollback_updates();
    rollback_additions();
    return result;
  }
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
  prepared_.reset();
  applied.clear();
  staged.clear();
  loader_.collect_unused();
  return engine::error::success;
}

engine::error prefab_runtime_instance::create(
    world& native_world, resource_service& loader, prefab_asset_ref prefab,
    const meta::registry_ref& registry, std::string_view instance_uuid, scene_node_id parent,
    const math::transform& root_transform, const std::vector<prefab_property_override>& overrides,
    std::unique_ptr<prefab_runtime_instance>& out_instance) noexcept {
  out_instance.reset();
  if (!registry || !prefab || prefab.get()->objects.empty()) {
    return engine::error::invalid_argument;
  }
  try {
    if (!engine::is_valid_prefab_author_address(
            {std::string(instance_uuid), prefab.get()->objects[0].uuid})) {
      return engine::error::invalid_argument;
    }
    std::uint64_t previous_camera = UINT64_C(0);
    const auto camera_query =
        engine::write_identifier<entity_id>(previous_camera, [&](auto& native_output) {
          return native_world.get_active_camera(native_output);
        });
    if (camera_query != engine::error::success && camera_query != engine::error::not_ready) {
      return camera_query;
    }
    camera_restore_guard camera_guard{.native_world = native_world, .previous = previous_camera};
    auto instance = std::make_unique<prefab_runtime_instance>(
        native_world, loader, std::move(prefab), std::string(instance_uuid));
    const auto& source = *instance->prefab_.get();
    structural_diff order;
    auto result = build_structural_diff({}, source.objects, order);
    if (result.failed())
      return result;
    result = instance->create_root(parent, root_transform);
    if (result.failed())
      return result;
    scene_subtree_guard guard(native_world, instance->root());
    for (const auto& addition : order.added) {
      const auto& object = source.objects[addition.new_index];
      mesh_renderer renderer;
      if (object.mesh_renderer) {
        auto mesh = loader.find<mesh_resource>(object.mesh_renderer->mesh_uri);
        if (!mesh)
          return mesh.error();
        auto material = loader.find<material_resource>(object.mesh_renderer->material_uri);
        if (!material)
          return material.error();
        renderer = {*mesh, *material};
      }
      const auto node_parent =
          object.parent_uuid ? instance->find_node(*object.parent_uuid) : instance->root();
      result =
          instance->create_node(addition.new_index, node_parent, renderer.mesh, renderer.material);
      if (result.failed())
        return result;
    }
    if (result == engine::error::success) {
      result = instance->apply_overrides(registry, overrides);
    }
    if (result != engine::error::success) {
      return result;
    }
    out_instance = std::move(instance);
    guard.release();
    camera_guard.committed = true;
    return engine::error::success;
  } catch (const std::bad_alloc&) {
    return engine::error::out_of_memory;
  } catch (...) {
    return engine::error::internal;
  }
}

namespace {
engine::error apply_candidate_object(const meta::registry_ref& state,
                                     const prefab_property_override& value,
                                     const engine::value& native, object_description& object) {
  auto result = engine::error::success;
  const auto is_type = [&](guid type) { return value.key.type_id == type; };
  if (is_type(transform_component_type)) {
    auto transform = to_transform(object);
    result = meta::write_property(state, value.key.type_id, value.key.field_id, &transform, native);
    if (result != engine::error::success) {
      return result;
    }
    object.translation = transform.translation;
    object.rotation = transform.rotation;
    object.scale = transform.scale;
  } else if (is_type(engine::camera_component_type)) {
    if (!object.camera) {
      return engine::error::not_found;
    }
    auto& camera = *object.camera;
    camera_properties properties;
    auto& settings = properties.settings;
    settings = {
        .fov = camera.vertical_field_of_view_radians,
        .near = camera.near_plane,
        .far = camera.far_plane,
    };
    result =
        meta::write_property(state, value.key.type_id, value.key.field_id, &properties, native);
    if (result != engine::error::success) {
      return result;
    }
    camera.vertical_field_of_view_radians = settings.fov;
    camera.near_plane = settings.near;
    camera.far_plane = settings.far;
  } else {
    // 当前对象格式只存储这些组件，不能将其他类型解释为同一候选内存布局。
    return engine::error::unsupported;
  }
  return engine::error::success;
}
}

engine::error prefab_runtime_instance::prepare_description(
    const prefab_description& source, const meta::registry_ref& registry,
    std::string_view instance_uuid, std::span<const prefab_property_override> overrides,
    prefab_description& output) noexcept try {
  auto candidate = source;
  const auto state = registry;
  if (!state) {
    return engine::error::invalid_handle;
  }
  for (const auto& value : overrides) {
    if (value.key.node.instance_uuid != instance_uuid) {
      return engine::error::invalid_argument;
    }
    const auto validation = validate_prefab_property_override(registry, value);
    if (validation != engine::error::success) {
      return validation;
    }
    auto found = std::ranges::find(candidate.objects, value.key.node.source_node_uuid,
                                   &object_description::uuid);
    if (found == candidate.objects.end()) {
      return engine::error::not_found;
    }
    if (!found->parent_uuid && value.key.type_id == transform_component_type)
      return error::invalid_argument;
    const auto result = apply_candidate_object(state, value, value.value, *found);
    if (result != engine::error::success) {
      return result;
    }
  }
  for (const auto& object : candidate.objects) {
    if (object.camera && object.camera->near_plane >= object.camera->far_plane) {
      return engine::error::invalid_argument;
    }
  }
  output = std::move(candidate);
  return engine::error::success;
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}

engine::error prefab_runtime_instance::apply_overrides(
    const meta::registry_ref& registry,
    std::span<const prefab_property_override> overrides) noexcept try {
  for (const auto& override_value : overrides) {
    if (override_value.key.node.instance_uuid != instance_uuid_) {
      return engine::error::invalid_argument;
    }
    const auto validation = validate_prefab_property_override(registry, override_value);
    if (validation != engine::error::success) {
      return validation;
    }
    const auto found = std::ranges::find(
        nodes_, override_value.key.node.source_node_uuid,
        [](const runtime_node& node) { return std::string_view(node.address.source_node_uuid); });
    if (found == nodes_.end()) {
      return engine::error::not_found;
    }
    if (found->node == root_node_ && override_value.key.type_id == transform_component_type)
      return error::invalid_argument;
    const auto result = engine::write_component_property(
        registry, native_world_, scene_object_entity(native_world_, found->node),
        override_value.key.type_id, override_value.key.field_id, override_value.value);
    if (result != engine::error::success) {
      return result;
    }
  }
  return engine::error::success;
} catch (...) {
  return engine::error::internal;
}

engine::error prefab_runtime_instance::create_root(scene_node_id parent,
                                                   const math::transform& transform) {
  if (!prefab_ || root_node_ || !nodes_.empty())
    return error::invalid_state;
  const auto& objects = prepared_ ? prepared_->objects : prefab_->objects;
  const auto root =
      std::ranges::find_if(objects, [](const auto& value) { return !value.parent_uuid; });
  if (root == objects.end() ||
      std::ranges::count_if(objects, [](const auto& value) { return !value.parent_uuid; }) != 1)
    return error::invalid_argument;
  nodes_.resize(objects.size());
  auto& member = nodes_[static_cast<std::size_t>(root - objects.begin())];
  member.address = {instance_uuid_, root->uuid};
  auto created = native_world_.object_create(parent, {instance_uuid_, {}}, transform);
  if (!created)
    return created.error();
  root_node_ = *created;
  member.node = root_node_;
  return error::success;
}
engine::error prefab_runtime_instance::create_node(std::size_t index, scene_node_id parent,
                                                   engine::mesh mesh, engine::material material) {
  if (!root_node_ || !prefab_ || index >= nodes_.size() || nodes_[index].creation_attempted)
    return error::invalid_state;
  const auto& source = prepared_ ? prepared_->objects[index] : prefab_->objects[index];
  auto& target = nodes_[index];
  target.address = {instance_uuid_, source.uuid};
  if (source.mesh_renderer) {
    if (!mesh || !material)
      return error::not_ready;
    if (!loader_.owns(mesh) || !loader_.owns(material))
      return error::invalid_argument;
  }
  target.creation_attempted = true;
  if (!source.parent_uuid) {
    if (target.node != root_node_)
      return error::invalid_state;
    return configure_scene_object(native_world_, root_node_, source, {mesh, material});
  }
  auto created = create_scene_object(native_world_, source, parent, {mesh, material});
  if (!created)
    return created.error();
  target.node = *created;
  return error::success;
}

engine::error
prefab_runtime_instance::apply_override(const meta::registry_ref& registry,
                                        const prefab_property_override& value) noexcept {
  return apply_overrides(registry, std::span(&value, 1U));
}

scene_node_id prefab_runtime_instance::find_node(std::string_view source_node_uuid) const noexcept {
  const auto found = std::ranges::find(nodes_, source_node_uuid, [](const runtime_node& node) {
    return std::string_view(node.address.source_node_uuid);
  });
  return found == nodes_.end() ? null_scene_node_id : found->node;
}

std::uint64_t
prefab_runtime_instance::find_entity(std::string_view source_node_uuid) const noexcept {
  const auto found = std::ranges::find(nodes_, source_node_uuid, [](const runtime_node& node) {
    return std::string_view(node.address.source_node_uuid);
  });
  return found == nodes_.end() ? UINT64_C(0) : scene_object_entity(native_world_, found->node);
}

const object_description*
prefab_runtime_instance::find_source_object(std::string_view source_node_uuid) const noexcept {
  const auto& objects = prefab_.get()->objects;
  const auto found = std::ranges::find(objects, source_node_uuid, &object_description::uuid);
  return found == objects.end() ? nullptr : &*found;
}

engine::error prefab_runtime_instance::get_node_info(std::size_t index,
                                                     node_info& out_info) const noexcept {
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
