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

prefab_runtime_instance::~prefab_runtime_instance() noexcept { rollback(); }

engine::error
prefab_runtime_instance::validate_reload(const prefab_asset_ref& candidate,
                                         const meta::registry_ref& registry,
                                         const std::vector<prefab_property_override>& overrides) {
  if (!candidate || !prefab_ || candidate.get()->uuid != prefab_.get()->uuid ||
      candidate.get()->objects.empty()) {
    return engine::error::invalid_argument;
  }
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
      if (target.entity != UINT64_C(0)) {
        (void)native_world_.entity_destroy(gneiss::entity_id{target.entity});
      }
      if (target.node != null_scene_node_id) {
        (void)native_world_.node_destroy(gneiss::scene_node_id{target.node});
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
    result = engine::write_identifier<entity_id>(target.entity, [&](auto& native_output) {
      return native_world_.entity_create(native_output);
    });
    if (result == engine::error::success) {
      result = native_world_.node_create(gneiss::scene_node_id{parent},
                                         gneiss::entity_id{target.entity}, target.node);
    }
    const auto transform = to_transform(source);
    if (result == engine::error::success) {
      result =
          native_world_.node_set_local_transform(gneiss::scene_node_id{target.node}, transform);
    }
    if (result == engine::error::success) {
      result = apply_camera(native_world_, target.entity, source.camera);
    }
    if (result == engine::error::success && source.mesh_renderer) {
      const mesh_renderer renderer{.mesh = target.mesh, .material = target.material};
      result = native_world_.entity_set_mesh_renderer(gneiss::entity_id{target.entity}, renderer);
    }

    if (result != engine::error::success) {
      if (target.entity != UINT64_C(0)) {
        (void)native_world_.entity_destroy(gneiss::entity_id{target.entity});
      }
      if (target.node != null_scene_node_id) {
        (void)native_world_.node_destroy(gneiss::scene_node_id{target.node});
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
          const mesh_renderer renderer{.mesh = nodes_[snapshot.old_index].mesh,
                                       .material = nodes_[snapshot.old_index].material};
          (void)native_world_.entity_set_mesh_renderer(gneiss::entity_id{target.entity}, renderer);
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
    applied.push_back(snapshot);
    const auto& source = candidate.get()->objects[update.new_index];
    if (has_change(update.changes, structural_change::parent)) {
      const auto parent = source.parent_uuid ? runtime_nodes.at(*source.parent_uuid) : root_node_;
      result = native_world_.node_reparent(gneiss::scene_node_id{target.node},
                                           gneiss::scene_node_id{parent});
    }
    if (result == engine::error::success &&
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

  auto previous_nodes = std::move(nodes_);
  nodes_ = std::move(staged);
  result = apply_overrides(registry, overrides);
  if (result != engine::error::success) {
    staged = std::move(nodes_);
    nodes_ = std::move(previous_nodes);
    rollback_updates();
    rollback_additions();
    return result;
  }
  for (const auto& removal : diff.removed) {
    const auto& target = previous_nodes[removal.old_index];
    (void)native_world_.entity_destroy(gneiss::entity_id{target.entity});
    (void)native_world_.node_destroy(gneiss::scene_node_id{target.node});
  }
  prefab_ = std::move(candidate);
  prepared_.reset();
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
    auto result = instance->stage_assets(*instance->prefab_.get());
    if (result == engine::error::success) {
      result = instance->commit(*instance->prefab_.get(), parent, root_transform);
    }
    if (result == engine::error::success) {
      result = instance->apply_overrides(registry, overrides);
    }
    if (result != engine::error::success) {
      return result;
    }
    out_instance = std::move(instance);
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
    const auto result = engine::write_component_property(
        registry, native_world_, found->entity, override_value.key.type_id,
        override_value.key.field_id, override_value.value);
    if (result != engine::error::success) {
      return result;
    }
  }
  return engine::error::success;
} catch (...) {
  return engine::error::internal;
}

engine::error prefab_runtime_instance::stage_assets(const prefab_description& description) {
  nodes_.reserve(description.objects.size());
  for (const auto& source : description.objects) {
    runtime_node target{};
    target.address = {instance_uuid_, source.uuid};
    target.name = source.name;
    if (source.mesh_renderer) {

      auto result = engine::error::success;
      {
        auto loaded = loader_.find<mesh_resource>(source.mesh_renderer->mesh_uri);
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
          auto loaded = loader_.find<material_resource>(source.mesh_renderer->material_uri);
          if (loaded)
            target.material = std::move(*loaded);
          else {
            target.material = {};
            result = loaded.error();
          }
        };
      }
      if (result != engine::error::success) {
        return result;
      }
    }
    nodes_.push_back(std::move(target));
  }
  return engine::error::success;
}

engine::error prefab_runtime_instance::commit(const prefab_description& description,
                                              scene_node_id parent,
                                              const math::transform& root_transform) {
  auto result = engine::write_identifier<entity_id>(root_entity_, [&](auto& native_output) {
    return native_world_.entity_create(native_output);
  });
  if (result == engine::error::success) {
    result = native_world_.node_create(gneiss::scene_node_id{parent},
                                       gneiss::entity_id{root_entity_}, root_node_);
  }
  if (result == engine::error::success) {
    result =
        native_world_.node_set_local_transform(gneiss::scene_node_id{root_node_}, root_transform);
  }
  if (result != engine::error::success) {
    return result;
  }

  std::unordered_map<std::string_view, scene_node_id> committed_nodes;
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
      result = commit_node(index, parent_node);
      if (result != engine::error::success) {
        return result;
      }
      committed_nodes.emplace(source.uuid, nodes_[index].node);
      committed[index] = true;
      ++committed_count;
      made_progress = true;
    }
    if (!made_progress) {
      return engine::error::internal;
    }
  }
  return engine::error::success;
}

engine::error prefab_runtime_instance::commit_node(std::size_t index, scene_node_id parent) {
  const auto& source = prepared_ ? prepared_->objects[index] : prefab_.get()->objects[index];
  auto& target = nodes_[index];
  target.parent = parent;

  auto result = engine::write_identifier<entity_id>(target.entity, [&](auto& native_output) {
    return native_world_.entity_create(native_output);
  });
  if (result == engine::error::success) {
    result = native_world_.node_create(gneiss::scene_node_id{parent},
                                       gneiss::entity_id{target.entity}, target.node);
  }
  const auto transform = to_transform(source);
  if (result == engine::error::success) {
    result = native_world_.node_set_local_transform(gneiss::scene_node_id{target.node}, transform);
  }
  if (result == engine::error::success && source.camera) {
    const math::camera_settings camera{.fov = source.camera->vertical_field_of_view_radians,
                                       .near = source.camera->near_plane,
                                       .far = source.camera->far_plane};
    result = native_world_.entity_configure_camera(gneiss::entity_id{target.entity}, camera);
    if (result == engine::error::success && source.camera->is_primary) {
      result = native_world_.set_active_camera(gneiss::entity_id{target.entity});
    }
  }
  if (result == engine::error::success && source.mesh_renderer) {
    const mesh_renderer renderer{.mesh = target.mesh, .material = target.material};
    result = native_world_.entity_set_mesh_renderer(gneiss::entity_id{target.entity}, renderer);
  }
  return result;
}

engine::error prefab_runtime_instance::create_root(scene_node_id parent,
                                                   const math::transform& transform) {
  if (!prefab_ || root_node_ != null_scene_node_id || !nodes_.empty()) {
    return engine::error::invalid_state;
  }
  nodes_.resize(prefab_.get()->objects.size());
  auto result = engine::write_identifier<entity_id>(root_entity_, [&](auto& native_output) {
    return native_world_.entity_create(native_output);
  });
  if (result == engine::error::success) {
    result = native_world_.node_create(gneiss::scene_node_id{parent},
                                       gneiss::entity_id{root_entity_}, root_node_);
  }
  if (result == engine::error::success) {
    result = native_world_.node_set_local_transform(gneiss::scene_node_id{root_node_}, transform);
  }
  return result;
}

engine::error prefab_runtime_instance::create_node(std::size_t index, scene_node_id parent,
                                                   engine::mesh mesh, engine::material material) {
  if (!root_node_ || !prefab_ || index >= nodes_.size() || nodes_[index].entity != UINT64_C(0) ||
      nodes_[index].creation_attempted) {
    return engine::error::invalid_state;
  }
  const auto& source = prefab_.get()->objects[index];
  auto& target = nodes_[index];
  target.address = {instance_uuid_, source.uuid};
  target.name = source.name;
  if (source.mesh_renderer) {
    if (!mesh || !material) {
      return engine::error::not_ready;
    }
    if (engine::asset_kind(mesh) != asset_type::mesh ||
        engine::asset_kind(material) != asset_type::material || !loader_.owns(mesh) ||
        !loader_.owns(material)) {
      return engine::error::invalid_argument;
    }
    target.mesh = std::move(mesh);
    target.material = std::move(material);
  }
  // 防止重复尝试已经部分创建的节点；失败由实例析构统一回收。
  target.creation_attempted = true;
  return commit_node(index, parent);
}

engine::error
prefab_runtime_instance::apply_override(const meta::registry_ref& registry,
                                        const prefab_property_override& value) noexcept {
  return apply_overrides(registry, std::span(&value, 1U));
}

void prefab_runtime_instance::rollback() noexcept {

  for (auto iterator = nodes_.rbegin(); iterator != nodes_.rend(); ++iterator) {
    if (iterator->entity != UINT64_C(0)) {
      (void)native_world_.entity_destroy(gneiss::entity_id{iterator->entity});
    }
    if (iterator->node != null_scene_node_id) {
      (void)native_world_.node_destroy(gneiss::scene_node_id{iterator->node});
    }
    iterator->mesh = {};
    iterator->material = {};
  }
  nodes_.clear();
  if (root_entity_ != UINT64_C(0)) {
    (void)native_world_.entity_destroy(gneiss::entity_id{root_entity_});
    root_entity_ = UINT64_C(0);
  }
  if (root_node_ != null_scene_node_id) {
    (void)native_world_.node_destroy(gneiss::scene_node_id{root_node_});
    root_node_ = null_scene_node_id;
  }
  prefab_ = {};
  loader_.collect_unused();
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
  return found == nodes_.end() ? UINT64_C(0) : found->entity;
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
  scene_node_id parent = source.parent;
  const auto result = native_world_.node_get_parent(gneiss::scene_node_id{source.node}, parent);
  if (result != engine::error::success) {
    return result;
  }
  out_info = {.address = &source.address,
              .name = source.name,
              .entity = source.entity,
              .node = source.node,
              .parent = parent};
  return engine::error::success;
}

} // namespace gneiss::engine
