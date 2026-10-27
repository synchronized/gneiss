// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/scene/prefab_runtime_instance.hpp"
#include "engine/core/result.hpp"
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

[[nodiscard]] gneiss::engine::result
to_native_value(const gneiss::engine::prefab_property_value& source,
                gneiss::engine::value& output) noexcept try {
  output = {};
  return std::visit(
      [&](const auto& payload) -> gneiss::engine::result {
        using type = std::decay_t<decltype(payload)>;
        if constexpr (std::is_same_v<type, std::monostate>)
          return gneiss::engine::result::invalid_argument;
        else if constexpr (std::is_same_v<type, std::string>)
          output.payload = payload;
        else if constexpr (std::is_same_v<type, std::array<std::uint8_t, 16>>)
          output.payload = gneiss::engine::guid::from_bytes(payload);
        else if constexpr (std::is_same_v<type, std::array<float, 3>>)
          output.payload = gneiss::engine::math::vector3{payload[0], payload[1], payload[2]};
        else if constexpr (std::is_same_v<type, std::array<float, 4>>)
          output.payload =
              gneiss::engine::math::quaternion_xyzw(payload[0], payload[1], payload[2], payload[3]);
        else
          output.payload = payload;
        return gneiss::engine::result::success;
      },
      source.payload);
} catch (const std::bad_alloc&) {
  return gneiss::engine::result::out_of_memory;
} catch (...) {
  return gneiss::engine::result::internal;
}

gneiss::engine::result
apply_camera(gneiss::engine::world& native_world, std::uint64_t entity,
             const std::optional<gneiss::engine::camera_description>& camera) noexcept {
  if (!camera) {
    const auto result = native_world.entity_remove_camera(gneiss::entity_id{entity});
    return result == gneiss::engine::result::not_found ? gneiss::engine::result::success : result;
  }
  const gneiss::engine::math::camera_settings value{camera->vertical_field_of_view_radians,
                                                    camera->near_plane, camera->far_plane};
  return native_world.entity_set_camera(gneiss::entity_id{entity}, value, camera->is_primary);
}

} // namespace

namespace gneiss::engine {

prefab_runtime_instance::prefab_runtime_instance(gneiss::engine::world& native_world,
                                                 gneiss::engine::render_asset_loader& loader,
                                                 gneiss::engine::prefab_asset_ref prefab,
                                                 std::string instance_uuid) noexcept
    : native_world_(native_world), loader_(loader), prefab_(std::move(prefab)),
      instance_uuid_(std::move(instance_uuid)) {}

prefab_runtime_instance::~prefab_runtime_instance() noexcept { rollback(); }

gneiss::engine::result prefab_runtime_instance::validate_reload(
    const gneiss::engine::prefab_asset_ref& candidate,
    const gneiss::engine::meta::registry_ref& registry,
    const std::vector<gneiss::engine::prefab_property_override>& overrides) {
  if (!candidate || !prefab_ || candidate.get()->uuid != prefab_.get()->uuid ||
      candidate.get()->objects.empty()) {
    return gneiss::engine::result::invalid_argument;
  }
  structural_diff diff;
  auto result = build_structural_diff(prefab_.get()->objects, candidate.get()->objects, diff);
  if (result != gneiss::engine::result::success) {
    return result;
  }

  std::vector<gneiss::engine::mesh> meshes(candidate.get()->objects.size());
  std::vector<gneiss::engine::material> materials(candidate.get()->objects.size());
  for (std::size_t index = 0U; index < candidate.get()->objects.size(); ++index) {
    const auto& source = candidate.get()->objects[index];
    if (!source.mesh_renderer) {
      continue;
    }
    result = [&] {
      render_asset_ref cached;
      const auto status =
          loader_.acquire_cached({source.mesh_renderer->mesh_uri, render_asset_type::mesh}, cached);
      if (status.ok())
        meshes[index] = render_asset_loader::mesh_ref(cached);
      return status;
    }();
    if (result == gneiss::engine::result::success) {
      result = [&] {
        render_asset_ref cached;
        const auto status = loader_.acquire_cached(
            {source.mesh_renderer->material_uri, render_asset_type::material}, cached);
        if (status.ok())
          materials[index] = render_asset_loader::material_ref(cached);
        return status;
      }();
    }
    if (result != gneiss::engine::result::success) {
      return result;
    }
  }

  const auto camera_type = gneiss::engine::camera_component_type;
  for (const auto& override_value : overrides) {
    if (override_value.key.node.instance_uuid != instance_uuid_) {
      return gneiss::engine::result::invalid_argument;
    }
    result = validate_prefab_property_override(registry, override_value);
    const auto source =
        std::ranges::find(candidate.get()->objects, override_value.key.node.source_node_uuid,
                          &gneiss::engine::object_description::uuid);
    if (result != gneiss::engine::result::success || source == candidate.get()->objects.end()) {
      return result == gneiss::engine::result::success ? gneiss::engine::result::not_found : result;
    }
    if (override_value.key.type_id == camera_type && !source->camera) {
      return gneiss::engine::result::invalid_state;
    }
  }
  return gneiss::engine::result::success;
}

gneiss::engine::result prefab_runtime_instance::reload(
    gneiss::engine::prefab_asset_ref candidate, const gneiss::engine::meta::registry_ref& registry,
    const std::vector<gneiss::engine::prefab_property_override>& overrides) {

  auto result = validate_reload(candidate, registry, overrides);
  if (result != gneiss::engine::result::success) {
    return result;
  }
  structural_diff diff;

  result = build_structural_diff(prefab_.get()->objects, candidate.get()->objects, diff);
  if (result != gneiss::engine::result::success) {
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
        const auto status = loader_.acquire_cached(
            {source.mesh_renderer->mesh_uri, render_asset_type::mesh}, cached);
        if (status.ok())
          target.mesh = render_asset_loader::mesh_ref(cached);
        return status;
      }();
      if (result == gneiss::engine::result::success) {
        result = [&] {
          render_asset_ref cached;
          const auto status = loader_.acquire_cached(
              {source.mesh_renderer->material_uri, render_asset_type::material}, cached);
          if (status.ok())
            target.material = render_asset_loader::material_ref(cached);
          return status;
        }();
      }
      if (result != gneiss::engine::result::success) {
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
  (void)gneiss::engine::write_identifier<entity_id>(active_camera_before, [&](auto& native_output) {
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
    result = gneiss::engine::write_identifier<entity_id>(target.entity, [&](auto& native_output) {
      return native_world_.entity_create(native_output);
    });
    if (result == gneiss::engine::result::success) {
      result = native_world_.node_create(gneiss::scene_node_id{parent},
                                         gneiss::entity_id{target.entity}, target.node);
    }
    const auto transform = to_transform(source);
    if (result == gneiss::engine::result::success) {
      result =
          native_world_.node_set_local_transform(gneiss::scene_node_id{target.node}, transform);
    }
    if (result == gneiss::engine::result::success) {
      result = apply_camera(native_world_, target.entity, source.camera);
    }
    if (result == gneiss::engine::result::success && source.mesh_renderer) {
      const gneiss::engine::mesh_renderer renderer{.mesh = target.mesh,
                                                   .material = target.material};
      result = native_world_.entity_set_mesh_renderer(gneiss::entity_id{target.entity}, renderer);
    }

    if (result != gneiss::engine::result::success) {
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
    gneiss::engine::math::transform transform = {};
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
        const auto& old_source = prefab_.get()->objects[snapshot.old_index];
        if (old_source.mesh_renderer) {
          const gneiss::engine::mesh_renderer renderer{.mesh = nodes_[snapshot.old_index].mesh,
                                                       .material =
                                                           nodes_[snapshot.old_index].material};
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
    if (result == gneiss::engine::result::success) {
      result = native_world_.node_get_local_transform(gneiss::scene_node_id{target.node},
                                                      snapshot.transform);
    }
    if (result == gneiss::engine::result::success &&
        has_change(update.changes, structural_change::camera)) {
      gneiss::engine::math::camera_settings camera;
      const auto camera_result =
          native_world_.entity_get_camera(gneiss::entity_id{target.entity}, camera);
      if (camera_result == gneiss::engine::result::success) {
        std::uint64_t active = UINT64_C(0);
        snapshot.camera = gneiss::engine::camera_description{
            .vertical_field_of_view_radians = camera.fov,
            .near_plane = camera.near,
            .far_plane = camera.far,
            .is_primary = gneiss::engine::write_identifier<entity_id>(
                              active,
                              [&](auto& native_output) {
                                return native_world_.get_active_camera(native_output);
                              }) == gneiss::engine::result::success &&
                          active == target.entity};
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
    const auto& source = candidate.get()->objects[update.new_index];
    if (has_change(update.changes, structural_change::parent)) {
      const auto parent = source.parent_uuid ? runtime_nodes.at(*source.parent_uuid) : root_node_;
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
      if (source.mesh_renderer) {
        const gneiss::engine::mesh_renderer renderer{.mesh = target.mesh,
                                                     .material = target.material};
        result = native_world_.entity_set_mesh_renderer(gneiss::entity_id{target.entity}, renderer);
      } else {
        result = native_world_.entity_remove_mesh_renderer(gneiss::entity_id{target.entity});
      }
    }
    if (result != gneiss::engine::result::success) {
      rollback_updates();
      rollback_additions();
      return result;
    }
  }

  auto previous_nodes = std::move(nodes_);
  nodes_ = std::move(staged);
  result = apply_overrides(registry, overrides);
  if (result != gneiss::engine::result::success) {
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
  loader_.release_unused();
  return gneiss::engine::result::success;
}

gneiss::engine::result prefab_runtime_instance::create(
    gneiss::engine::world& native_world, gneiss::engine::render_asset_loader& loader,
    gneiss::engine::prefab_asset_ref prefab, const gneiss::engine::meta::registry_ref& registry,
    std::string_view instance_uuid, scene_node_id parent,
    const gneiss::engine::math::transform& root_transform,
    const std::vector<gneiss::engine::prefab_property_override>& overrides,
    std::unique_ptr<prefab_runtime_instance>& out_instance) noexcept {
  out_instance.reset();
  if (!registry || !prefab || prefab.get()->objects.empty()) {
    return gneiss::engine::result::invalid_argument;
  }
  try {
    if (!gneiss::engine::is_valid_prefab_author_address(
            {std::string(instance_uuid), prefab.get()->objects[0].uuid})) {
      return gneiss::engine::result::invalid_argument;
    }
    std::uint64_t previous_camera = UINT64_C(0);
    const auto camera_query =
        gneiss::engine::write_identifier<entity_id>(previous_camera, [&](auto& native_output) {
          return native_world.get_active_camera(native_output);
        });
    if (camera_query != gneiss::engine::result::success &&
        camera_query != gneiss::engine::result::not_ready) {
      return camera_query;
    }
    camera_restore_guard camera_guard{.native_world = native_world, .previous = previous_camera};
    auto instance = std::make_unique<prefab_runtime_instance>(
        native_world, loader, std::move(prefab), std::string(instance_uuid));
    auto result = instance->stage_assets(*instance->prefab_.get());
    if (result == gneiss::engine::result::success) {
      result = instance->commit(*instance->prefab_.get(), parent, root_transform);
    }
    if (result == gneiss::engine::result::success) {
      result = instance->apply_overrides(registry, overrides);
    }
    if (result != gneiss::engine::result::success) {
      return result;
    }
    out_instance = std::move(instance);
    camera_guard.committed = true;
    return gneiss::engine::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

namespace {
gneiss::engine::result apply_candidate_object(const gneiss::engine::meta::registry_ref& state,
                                              const gneiss::engine::prefab_property_override& value,
                                              const gneiss::engine::value& native,
                                              gneiss::engine::object_description& object) {
  auto result = gneiss::engine::result::success;
  const auto is_type = [&](gneiss::engine::guid type) { return value.key.type_id == type; };
  if (is_type(gneiss::engine::transform_component_type)) {
    auto transform = to_transform(object);
    result = gneiss::engine::meta::write_property(state, value.key.type_id, value.key.field_id,
                                                  &transform, native);
    if (result != gneiss::engine::result::success) {
      return result;
    }
    object.translation = transform.translation;
    object.rotation = transform.rotation;
    object.scale = transform.scale;
  } else if (is_type(gneiss::engine::camera_component_type)) {
    if (!object.camera) {
      return gneiss::engine::result::not_found;
    }
    auto& camera = *object.camera;
    gneiss::engine::camera_properties properties;
    auto& settings = properties.settings;
    settings = {
        .fov = camera.vertical_field_of_view_radians,
        .near = camera.near_plane,
        .far = camera.far_plane,
    };
    result = gneiss::engine::meta::write_property(state, value.key.type_id, value.key.field_id,
                                                  &properties, native);
    if (result != gneiss::engine::result::success) {
      return result;
    }
    camera.vertical_field_of_view_radians = settings.fov;
    camera.near_plane = settings.near;
    camera.far_plane = settings.far;
  } else {
    // 当前对象格式只存储这些组件，不能将其他类型解释为同一候选内存布局。
    return gneiss::engine::result::unsupported;
  }
  return gneiss::engine::result::success;
}
}

gneiss::engine::result prefab_runtime_instance::prepare_description(
    const gneiss::engine::prefab_description& source,
    const gneiss::engine::meta::registry_ref& registry, std::string_view instance_uuid,
    std::span<const gneiss::engine::prefab_property_override> overrides,
    gneiss::engine::prefab_description& output) noexcept try {
  auto candidate = source;
  const auto state = registry;
  if (!state) {
    return gneiss::engine::result::invalid_handle;
  }
  for (const auto& value : overrides) {
    if (value.key.node.instance_uuid != instance_uuid) {
      return gneiss::engine::result::invalid_argument;
    }
    const auto validation = validate_prefab_property_override(registry, value);
    if (validation != gneiss::engine::result::success) {
      return validation;
    }
    auto found = std::ranges::find(candidate.objects, value.key.node.source_node_uuid,
                                   &gneiss::engine::object_description::uuid);
    if (found == candidate.objects.end()) {
      return gneiss::engine::result::not_found;
    }
    gneiss::engine::value native = gneiss::engine::value{};
    auto result = to_native_value(value.value, native);
    if (result != gneiss::engine::result::success) {
      return result;
    }
    result = apply_candidate_object(state, value, native, *found);
    if (result != gneiss::engine::result::success) {
      return result;
    }
  }
  for (const auto& object : candidate.objects) {
    if (object.camera && object.camera->near_plane >= object.camera->far_plane) {
      return gneiss::engine::result::invalid_argument;
    }
  }
  output = std::move(candidate);
  return gneiss::engine::result::success;
} catch (const std::bad_alloc&) {
  return gneiss::engine::result::out_of_memory;
} catch (...) {
  return gneiss::engine::result::internal;
}

gneiss::engine::result prefab_runtime_instance::apply_overrides(
    const gneiss::engine::meta::registry_ref& registry,
    std::span<const gneiss::engine::prefab_property_override> overrides) noexcept try {
  for (const auto& override_value : overrides) {
    if (override_value.key.node.instance_uuid != instance_uuid_) {
      return gneiss::engine::result::invalid_argument;
    }
    const auto validation = validate_prefab_property_override(registry, override_value);
    if (validation != gneiss::engine::result::success) {
      return validation;
    }
    const auto found = std::ranges::find(
        nodes_, override_value.key.node.source_node_uuid,
        [](const runtime_node& node) { return std::string_view(node.address.source_node_uuid); });
    if (found == nodes_.end()) {
      return gneiss::engine::result::not_found;
    }
    gneiss::engine::value native_value = gneiss::engine::value{};
    auto result = to_native_value(override_value.value, native_value);
    if (result == gneiss::engine::result::success) {
      result = gneiss::engine::write_component_property(registry, native_world_, found->entity,
                                                        override_value.key.type_id,
                                                        override_value.key.field_id, native_value);
    }
    if (result != gneiss::engine::result::success) {
      return result;
    }
  }
  return gneiss::engine::result::success;
} catch (...) {
  return gneiss::engine::result::internal;
}

gneiss::engine::result
prefab_runtime_instance::stage_assets(const gneiss::engine::prefab_description& description) {
  nodes_.reserve(description.objects.size());
  for (const auto& source : description.objects) {
    runtime_node target{};
    target.address = {instance_uuid_, source.uuid};
    target.name = source.name;
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
    nodes_.push_back(std::move(target));
  }
  return gneiss::engine::result::success;
}

gneiss::engine::result
prefab_runtime_instance::commit(const gneiss::engine::prefab_description& description,
                                scene_node_id parent,
                                const gneiss::engine::math::transform& root_transform) {
  auto result = gneiss::engine::write_identifier<entity_id>(root_entity_, [&](auto& native_output) {
    return native_world_.entity_create(native_output);
  });
  if (result == gneiss::engine::result::success) {
    result = native_world_.node_create(gneiss::scene_node_id{parent},
                                       gneiss::entity_id{root_entity_}, root_node_);
  }
  if (result == gneiss::engine::result::success) {
    result =
        native_world_.node_set_local_transform(gneiss::scene_node_id{root_node_}, root_transform);
  }
  if (result != gneiss::engine::result::success) {
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
      if (result != gneiss::engine::result::success) {
        return result;
      }
      committed_nodes.emplace(source.uuid, nodes_[index].node);
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

gneiss::engine::result prefab_runtime_instance::commit_node(std::size_t index,
                                                            scene_node_id parent) {
  const auto& source = prepared_ ? prepared_->objects[index] : prefab_.get()->objects[index];
  auto& target = nodes_[index];
  target.parent = parent;

  auto result =
      gneiss::engine::write_identifier<entity_id>(target.entity, [&](auto& native_output) {
        return native_world_.entity_create(native_output);
      });
  if (result == gneiss::engine::result::success) {
    result = native_world_.node_create(gneiss::scene_node_id{parent},
                                       gneiss::entity_id{target.entity}, target.node);
  }
  const auto transform = to_transform(source);
  if (result == gneiss::engine::result::success) {
    result = native_world_.node_set_local_transform(gneiss::scene_node_id{target.node}, transform);
  }
  if (result == gneiss::engine::result::success && source.camera) {
    const gneiss::engine::math::camera_settings camera{
        .fov = source.camera->vertical_field_of_view_radians,
        .near = source.camera->near_plane,
        .far = source.camera->far_plane};
    result = native_world_.entity_configure_camera(gneiss::entity_id{target.entity}, camera);
    if (result == gneiss::engine::result::success && source.camera->is_primary) {
      result = native_world_.set_active_camera(gneiss::entity_id{target.entity});
    }
  }
  if (result == gneiss::engine::result::success && source.mesh_renderer) {
    const gneiss::engine::mesh_renderer renderer{.mesh = target.mesh, .material = target.material};
    result = native_world_.entity_set_mesh_renderer(gneiss::entity_id{target.entity}, renderer);
  }
  return result;
}

gneiss::engine::result
prefab_runtime_instance::create_root(scene_node_id parent,
                                     const gneiss::engine::math::transform& transform) {
  if (!prefab_ || root_node_ != null_scene_node_id || !nodes_.empty()) {
    return gneiss::engine::result::invalid_state;
  }
  nodes_.resize(prefab_.get()->objects.size());
  auto result = gneiss::engine::write_identifier<entity_id>(root_entity_, [&](auto& native_output) {
    return native_world_.entity_create(native_output);
  });
  if (result == gneiss::engine::result::success) {
    result = native_world_.node_create(gneiss::scene_node_id{parent},
                                       gneiss::entity_id{root_entity_}, root_node_);
  }
  if (result == gneiss::engine::result::success) {
    result = native_world_.node_set_local_transform(gneiss::scene_node_id{root_node_}, transform);
  }
  return result;
}

gneiss::engine::result prefab_runtime_instance::create_node(std::size_t index, scene_node_id parent,
                                                            gneiss::engine::mesh mesh,
                                                            gneiss::engine::material material) {
  if (!root_node_ || !prefab_ || index >= nodes_.size() || nodes_[index].entity != UINT64_C(0) ||
      nodes_[index].creation_attempted) {
    return gneiss::engine::result::invalid_state;
  }
  const auto& source = prefab_.get()->objects[index];
  auto& target = nodes_[index];
  target.address = {instance_uuid_, source.uuid};
  target.name = source.name;
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
  // 防止重复尝试已经部分创建的节点；失败由实例析构统一回收。
  target.creation_attempted = true;
  return commit_node(index, parent);
}

gneiss::engine::result prefab_runtime_instance::apply_override(
    const gneiss::engine::meta::registry_ref& registry,
    const gneiss::engine::prefab_property_override& value) noexcept {
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
  loader_.release_unused();
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

const gneiss::engine::object_description*
prefab_runtime_instance::find_source_object(std::string_view source_node_uuid) const noexcept {
  const auto& objects = prefab_.get()->objects;
  const auto found =
      std::ranges::find(objects, source_node_uuid, &gneiss::engine::object_description::uuid);
  return found == objects.end() ? nullptr : &*found;
}

gneiss::engine::result prefab_runtime_instance::get_node_info(std::size_t index,
                                                              node_info& out_info) const noexcept {
  out_info = {};
  if (index >= nodes_.size()) {
    return gneiss::engine::result::not_found;
  }
  const auto& source = nodes_[index];
  scene_node_id parent = source.parent;
  const auto result = native_world_.node_get_parent(gneiss::scene_node_id{source.node}, parent);
  if (result != gneiss::engine::result::success) {
    return result;
  }
  out_info = {.address = &source.address,
              .name = source.name,
              .entity = source.entity,
              .node = source.node,
              .parent = parent};
  return gneiss::engine::result::success;
}

} // namespace gneiss::engine
