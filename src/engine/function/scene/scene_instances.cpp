// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/scene/scene_instances.hpp"
#include "engine/function/scene/scene_instance.hpp"

#include <atomic>
#include <limits>
#include <new>
#include <utility>

namespace gneiss::engine {
namespace {
std::uint16_t allocate_domain() noexcept {
  static std::atomic_uint32_t next_domain{UINT32_C(1024)};
  const auto value = next_domain.fetch_add(1U, std::memory_order_relaxed);
  return value <= std::numeric_limits<std::uint16_t>::max() ? static_cast<std::uint16_t>(value)
                                                            : UINT16_C(0);
}

} // namespace

result<prefab_header_snapshot> scene_instances::capture_prefab_header(scene_instance_id instance,
                                                                      guid uuid) const noexcept
    try {
  if (!native_world_.is_owner_thread())
    return error::invalid_state;
  auto* slot = instances_.get(instance, resource_type::scene_instance);
  if (!slot || !*slot)
    return error::invalid_handle;
  prefab_header_snapshot output{};
  const auto status = (*slot)->capture_prefab_instance_header(uuid, output.author, output.source);
  if (status.failed())
    return status;
  return output;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}

result<scene_mesh_binding> scene_instances::capture_prefab_binding(scene_instance_id instance,
                                                                   guid uuid,
                                                                   std::size_t index) const noexcept
    try {
  if (!native_world_.is_owner_thread())
    return error::invalid_state;
  auto* slot = instances_.get(instance, resource_type::scene_instance);
  if (!slot || !*slot)
    return error::invalid_handle;
  scene_mesh_binding output{};
  const auto status = (*slot)->capture_prefab_instance_binding(uuid, index, output);
  if (status.failed())
    return status;
  return output;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}

result<scene_node_id> scene_instances::instantiate_prefab_ready(
    scene_instance_id instance, const object_description& author, prefab_asset_ref prefab,
    std::span<const scene_mesh_binding> bindings, bool replacing) noexcept try {
  if (!native_world_.is_owner_thread())
    return error::invalid_state;
  auto* slot = instances_.get(instance, resource_type::scene_instance);
  if (!slot || !*slot)
    return error::invalid_handle;
  scene_node_id output{};
  const auto status =
      (*slot)->instantiate_prefab_ready(author, std::move(prefab), bindings, replacing, output);
  if (status.failed())
    return status;
  return output;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
error scene_instances::remove_prefab_instance(scene_instance_id instance,
                                              gneiss::engine::guid uuid) noexcept {
  if (!native_world_.is_owner_thread())
    return error::invalid_state;
  auto* slot = instances_.get(instance, resource_type::scene_instance);
  return !slot || !*slot ? error::invalid_handle : (*slot)->remove_prefab_instance(uuid);
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
                                                    prefab_asset_ref candidate,
                                                    scene_diagnostic* diagnostic) noexcept try {
  if (diagnostic)
    *diagnostic = {};
  if (!native_world_.is_owner_thread())
    return error::invalid_state;
  if (!candidate)
    return error::invalid_argument;
  auto* value = instances_.get(instance, resource_type::scene_instance);
  return value && *value ? (*value)->reload_prefab(uri, std::move(candidate), diagnostic)
                         : error::invalid_handle;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}

result<scene_instance_id> scene_instances::create_empty(guid scene_uuid) noexcept try {
  if (!native_world_.is_owner_thread())
    return error::invalid_state;
  if (!is_valid())
    return error::invalid_argument;
  if (!scene_uuid)
    return error::invalid_argument;
  auto instance = std::make_unique<scene_instance>(native_world_, loader_);
  instance->initialize_staged(scene_uuid);
  return instances_.create(resource_type::scene_instance, std::move(instance));
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}

result<scene_description>
scene_instances::capture_description(scene_instance_id instance) const noexcept try {
  if (!native_world_.is_owner_thread())
    return error::invalid_state;
  auto* slot = instances_.get(instance, resource_type::scene_instance);
  if (!slot || !*slot)
    return error::invalid_handle;
  scene_description output{};
  const auto status = (*slot)->capture_description(output);
  if (status.failed())
    return status;
  return output;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}

result<std::string> scene_instances::serialize(scene_instance_id instance) const noexcept try {
  if (!native_world_.is_owner_thread())
    return error::invalid_state;
  auto* slot = instances_.get(instance, resource_type::scene_instance);
  if (!slot || !*slot)
    return error::invalid_handle;
  std::string output{};
  const auto status = (*slot)->serialize(output);
  if (status.failed())
    return status;
  return output;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}

engine::error scene_instances::unload(scene_instance_id instance) noexcept {
  return instances_.destroy(instance, resource_type::scene_instance);
}

result<scene_node_id> scene_instances::find_node(scene_instance_id instance,
                                                 guid uuid) const noexcept try {
  if (!native_world_.is_owner_thread())
    return error::invalid_state;
  const auto* slot = instances_.get(instance, resource_type::scene_instance);
  if (!slot || !*slot)
    return error::invalid_handle;
  const auto node = (*slot)->find_node(uuid);
  if (node == null_scene_node_id)
    return error::not_found;
  return node;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}

result<std::uint64_t> scene_instances::get_node_count(scene_instance_id instance) const noexcept
    try {
  if (!native_world_.is_owner_thread())
    return error::invalid_state;
  const auto* slot = instances_.get(instance, resource_type::scene_instance);
  if (!slot || !*slot)
    return error::invalid_handle;
  return (*slot)->node_count();
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
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

result<scene_node_info> scene_instances::get_node_info(scene_instance_id instance,
                                                       std::uint64_t index,
                                                       node_query_detail detail) const noexcept
    try {
  if (!native_world_.is_owner_thread())
    return error::invalid_state;
  auto* slot = instances_.get(instance, resource_type::scene_instance);
  if (!slot || !*slot)
    return error::invalid_handle;
  scene_node_info output{};
  const auto status = (*slot)->get_node_info(index, output, detail);
  if (status.failed())
    return status;
  return output;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}

result<scene_node_id> scene_instances::create_node(scene_instance_id instance,
                                                   const node_creation& desc) noexcept try {
  if (!native_world_.is_owner_thread())
    return error::invalid_state;
  auto* slot = instances_.get(instance, resource_type::scene_instance);
  if (!slot || !*slot)
    return error::invalid_handle;
  scene_node_id output{};
  const auto status = (*slot)->create_node(desc, &output);
  if (status.failed())
    return status;
  return output;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
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

result<scene_subtree_snapshot> scene_instances::capture_subtree(scene_instance_id instance,
                                                                scene_node_id root) const noexcept
    try {
  if (!native_world_.is_owner_thread())
    return error::invalid_state;
  auto* slot = instances_.get(instance, resource_type::scene_instance);
  if (!slot || !*slot)
    return error::invalid_handle;
  scene_subtree_snapshot output{};
  const auto status = (*slot)->capture_subtree(root, output);
  if (status.failed())
    return status;
  return output;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}

result<scene_node_id>
scene_instances::restore_subtree(scene_instance_id instance, const scene_subtree_snapshot& snapshot,
                                 scene_node_id parent,
                                 std::span<const uuid_mapping> mappings) noexcept try {
  if (!native_world_.is_owner_thread())
    return error::invalid_state;
  auto* slot = instances_.get(instance, resource_type::scene_instance);
  if (!slot || !*slot)
    return error::invalid_handle;
  scene_node_id output{};
  const auto status = (*slot)->restore_subtree(snapshot, parent, mappings, &output);
  if (status.failed())
    return status;
  return output;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
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

result<scene_node_id>
scene_instances::create_mesh_renderer_node(scene_instance_id instance,
                                           const mesh_renderer_node_creation& desc) noexcept try {
  if (!native_world_.is_owner_thread())
    return error::invalid_state;
  auto* slot = instances_.get(instance, resource_type::scene_instance);
  if (!slot || !*slot)
    return error::invalid_handle;
  scene_node_id output{};
  const auto status = (*slot)->create_mesh_renderer_node(desc, &output);
  if (status.failed())
    return status;
  return output;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}

result<scene_node_id>
scene_instances::create_mesh_renderer_node_ready(scene_instance_id instance,
                                                 const mesh_renderer_node_creation& desc,
                                                 const scene_mesh_binding& prepared) noexcept try {
  if (!native_world_.is_owner_thread())
    return error::invalid_state;
  auto* slot = instances_.get(instance, resource_type::scene_instance);
  if (!slot || !*slot)
    return error::invalid_handle;
  scene_node_id output{};
  const auto status = (*slot)->create_mesh_renderer_node(desc, &output, &prepared);
  if (status.failed())
    return status;
  return output;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}

result<scene_mesh_binding> scene_instances::capture_mesh_binding(scene_instance_id instance,
                                                                 scene_node_id node) const noexcept
    try {
  if (!native_world_.is_owner_thread())
    return error::invalid_state;
  auto* slot = instances_.get(instance, resource_type::scene_instance);
  if (!slot || !*slot)
    return error::invalid_handle;
  scene_mesh_binding output{};
  const auto status = (*slot)->capture_mesh_binding(node, output);
  if (status.failed())
    return status;
  return output;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}

engine::error
scene_instances::replace_mesh_binding(scene_instance_id instance, scene_node_id node,
                                      const scene_mesh_binding& expected,
                                      const scene_mesh_binding& replacement) noexcept {
  const auto owner = native_world_.replace_mesh_renderers({});
  if (owner != engine::error::success) {
    return owner;
  }
  if ((replacement.mesh && !loader_.owns(replacement.mesh)) ||
      (replacement.material && !loader_.owns(replacement.material))) {
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

result<prefab_component_state>
scene_instances::capture_component_override(scene_instance_id instance, scene_node_id node,
                                            guid type) noexcept {
  if (!native_world_.is_owner_thread())
    return error::invalid_state;
  auto* value = instances_.get(instance, resource_type::scene_instance);
  if (!value || !*value)
    return error::invalid_handle;
  return (*value)->capture_component_override(node, type);
}
error scene_instances::set_component_override(scene_instance_id instance, scene_node_id node,
                                              guid type,
                                              const prefab_component_state& state) noexcept {
  if (!native_world_.is_owner_thread())
    return error::invalid_state;
  auto* value = instances_.get(instance, resource_type::scene_instance);
  if (!value || !*value)
    return error::invalid_handle;
  return (*value)->set_component_override(node, type, state);
}
error scene_instances::add_component_description(
    scene_instance_id instance, scene_node_id node,
    const component_description& description) noexcept {
  if (!native_world_.is_owner_thread())
    return error::invalid_state;
  auto* value = instances_.get(instance, resource_type::scene_instance);
  if (!value || !*value)
    return error::invalid_handle;
  return (*value)->add_component_description(node, description);
}
error scene_instances::remove_component_description(scene_instance_id instance, scene_node_id node,
                                                    guid type) noexcept {
  if (!native_world_.is_owner_thread())
    return error::invalid_state;
  auto* value = instances_.get(instance, resource_type::scene_instance);
  if (!value || !*value)
    return error::invalid_handle;
  return (*value)->remove_component_description(node, type);
}
error scene_instances::set_component_description(
    scene_instance_id instance, scene_node_id node,
    const component_description& description) noexcept {
  if (!native_world_.is_owner_thread())
    return error::invalid_state;
  auto* value = instances_.get(instance, resource_type::scene_instance);
  if (!value || !*value)
    return error::invalid_handle;
  return (*value)->set_component_description(node, description);
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
