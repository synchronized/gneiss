// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/scene/scene_instances.hpp"
#include "engine/function/scene/scene_instance_state.hpp"
#include "engine/function/world/scene_instance.hpp"

#include <algorithm>
#include <atomic>
#include <limits>
#include <new>
#include <type_traits>
#include <utility>

namespace gneiss::engine {
result<scene_instance> scene_instance::borrow(world& owner, scene_instance_id id) noexcept {
  if (!owner.is_owner_thread())
    return error::invalid_state;
  if (!owner.scenes())
    return error::invalid_handle;
  auto count = owner.scenes()->get_node_count(id);
  if (!count)
    return count.error();
  scene_instance result;
  result.owner_ = &owner;
  result.id_ = id;
  return result;
}
bool scene_instance::is_valid() const noexcept { return size().has_value(); }
result<std::uint64_t> scene_instance::size() const noexcept {
  if (!owner_ || !owner_->scenes())
    return error::invalid_handle;
  return owner_->scenes()->get_node_count(id_);
}
result<game_object> scene_instance::find(guid uuid) const noexcept {
  if (!owner_ || !owner_->scenes())
    return error::invalid_handle;
  auto node = owner_->scenes()->find_node(id_, uuid);
  if (!node)
    return node.error();
  return owner_->object(entity_id{node->get()});
}
result<game_object> scene_instance::create(scene_node_identity identity,
                                           const math::transform& transform,
                                           game_object parent) const {
  if (!owner_ || !owner_->scenes())
    return error::invalid_handle;
  if (parent.owner() && parent.owner() != owner_)
    return error::invalid_handle;
  auto created = owner_->scenes()->create_node(id_, {.uuid = identity.uuid,
                                                     .name = identity.name,
                                                     .parent = scene_node_id{parent.entity().get()},
                                                     .local_transform = transform});
  if (!created)
    return created.error();
  return owner_->object(entity_id{created->get()});
}
error scene_instance::unload() const noexcept {
  if (!owner_ || !owner_->scenes())
    return error::invalid_handle;
  return owner_->scenes()->unload(id_);
}

namespace {
std::uint16_t allocate_domain() noexcept {
  static std::atomic_uint32_t next_domain{UINT32_C(1024)};
  const auto value = next_domain.fetch_add(1U, std::memory_order_relaxed);
  return value <= std::numeric_limits<std::uint16_t>::max() ? static_cast<std::uint16_t>(value)
                                                            : UINT16_C(0);
}

} // namespace

result<scene_instances*> scene_instances::attach(world& owner, asset_manager& assets) {
  if (!owner.is_owner_thread() || owner.scenes_)
    return error::invalid_state;
  auto candidate = std::unique_ptr<scene_instances>(new scene_instances(owner, assets));
  if (!candidate->is_valid())
    return error::invalid_state;
  owner.scenes_ = std::move(candidate);
  return owner.scenes_.get();
}

scene_instances::scene_instances(world& native_world, asset_manager& loader) noexcept
    : native_world_(native_world), loader_(loader), domain_(allocate_domain()),
      instances_(domain_) {}

scene_instances::~scene_instances() noexcept = default;

result<scene_instance_state*> scene_instances::resolve(scene_instance_id instance) const noexcept {
  if (!native_world_.is_owner_thread())
    return error::invalid_state;
  const auto* slot = instances_.get(instance, resource_type::scene_instance);
  if (!slot || !*slot)
    return error::invalid_handle;
  (*slot)->prune_destroyed_members();
  return slot->get();
}

error scene_instances::validate_reparent(scene_node_id node, scene_node_id parent) noexcept {
  if (!parent)
    return error::success;
  // 临时/非场景对象可能位于成员之间；检查祖先和整棵移动子树，不能只比较两个根。
  const scene_instance_state* parent_owner = nullptr;
  for (auto cursor = parent; cursor && !parent_owner;) {
    instances_.for_each(resource_type::scene_instance, [&](const instance_ptr& instance) {
      if (instance->contains_editable_node(cursor))
        parent_owner = instance.get();
    });
    scene_node_id ancestor;
    if (native_world_.node_get_parent(cursor, ancestor).failed())
      return error::invalid_handle;
    cursor = ancestor;
  }
  if (!parent_owner)
    return error::success;
  bool crosses_scene = false;
  instances_.for_each(resource_type::scene_instance, [&](const instance_ptr& instance) {
    if (instance.get() == parent_owner || crosses_scene)
      return;
    for (const auto& member : instance->objects_) {
      for (auto cursor = member.node; cursor;) {
        if (cursor == node) {
          crosses_scene = true;
          return;
        }
        scene_node_id ancestor;
        if (native_world_.node_get_parent(cursor, ancestor).failed())
          break;
        cursor = ancestor;
      }
    }
  });
  return crosses_scene ? error::invalid_argument : error::success;
}

result<scene_instance_id> scene_instances::create_empty(guid scene_uuid) {
  if (!native_world_.is_owner_thread())
    return error::invalid_state;
  if (!is_valid())
    return error::invalid_argument;
  if (!scene_uuid)
    return error::invalid_argument;
  auto instance = std::make_unique<scene_instance_state>(native_world_, loader_);
  instance->initialize_staged(scene_uuid);
  return instances_.create(resource_type::scene_instance, std::move(instance));
}

engine::error scene_instances::unload(scene_instance_id instance) noexcept {
  if (!native_world_.is_owner_thread())
    return error::invalid_state;
  return instances_.destroy(instance, resource_type::scene_instance);
}

result<scene_node_id> scene_instances::find_node(scene_instance_id instance, guid uuid) const {
  if (!native_world_.is_owner_thread())
    return error::invalid_state;
  auto selected = resolve(instance);
  if (!selected)
    return selected.error();
  auto* slot = *selected;
  const auto node = slot->find_node(uuid);
  if (node == null_scene_node_id)
    return error::not_found;
  return node;
}

result<std::uint64_t> scene_instances::get_node_count(scene_instance_id instance) const {
  if (!native_world_.is_owner_thread())
    return error::invalid_state;
  auto selected = resolve(instance);
  if (!selected)
    return selected.error();
  auto* slot = *selected;
  return slot->node_count();
}

engine::error scene_instances::set_node_transform(scene_instance_id instance,
                                                  scene_node_id node_value,
                                                  const math::transform& transform) {
  auto node = node_value;
  {
    auto selected = resolve(instance);
    if (!selected)
      return selected.error();
    auto* value = *selected;
    return value->set_node_transform(node, transform);
  }
}

result<scene_node_info> scene_instances::get_node_info(scene_instance_id instance,
                                                       std::uint64_t index,
                                                       node_query_detail detail) const {
  if (!native_world_.is_owner_thread())
    return error::invalid_state;
  auto selected = resolve(instance);
  if (!selected)
    return selected.error();
  auto* slot = *selected;
  return slot->get_node_info(index, detail);
}

result<scene_node_id> scene_instances::create_node(scene_instance_id instance,
                                                   const node_creation& desc) {
  if (!native_world_.is_owner_thread())
    return error::invalid_state;
  auto selected = resolve(instance);
  if (!selected)
    return selected.error();
  auto* slot = *selected;
  return slot->create_node(desc);
}

engine::error scene_instances::set_node_name(scene_instance_id instance, scene_node_id node_value,
                                             std::string_view name) {
  auto node = node_value;
  {
    auto selected = resolve(instance);
    if (!selected)
      return selected.error();
    auto* value = *selected;
    return value->set_node_name(node, name);
  }
}

engine::error scene_instances::reparent_node(scene_instance_id instance, scene_node_id node_value,
                                             scene_node_id parent_value) {
  auto node = node_value;
  auto parent = parent_value;
  {
    auto selected = resolve(instance);
    if (!selected)
      return selected.error();
    auto* value = *selected;
    return value->reparent_node(node, parent);
  }
}

engine::error scene_instances::destroy_subtree(scene_instance_id instance,
                                               scene_node_id root_value) {
  auto root = root_value;
  {
    auto selected = resolve(instance);
    if (!selected)
      return selected.error();
    auto* value = *selected;
    return value->destroy_subtree(root);
  }
}

result<scene_node_id>
scene_instances::create_mesh_renderer_node(scene_instance_id instance,
                                           const mesh_renderer_node_creation& desc) {
  if (!native_world_.is_owner_thread())
    return error::invalid_state;
  auto selected = resolve(instance);
  if (!selected)
    return selected.error();
  auto* slot = *selected;
  return slot->create_mesh_renderer_node(desc);
}

result<scene_node_id>
scene_instances::create_mesh_renderer_node_ready(scene_instance_id instance,
                                                 const mesh_renderer_node_creation& desc,
                                                 const scene_mesh_binding& prepared) {
  if (!native_world_.is_owner_thread())
    return error::invalid_state;
  auto selected = resolve(instance);
  if (!selected)
    return selected.error();
  auto* slot = *selected;
  return slot->create_mesh_renderer_node(desc, &prepared);
}

result<scene_mesh_binding> scene_instances::capture_mesh_binding(scene_instance_id instance,
                                                                 scene_node_id node) const {
  if (!native_world_.is_owner_thread())
    return error::invalid_state;
  auto selected = resolve(instance);
  if (!selected)
    return selected.error();
  auto* slot = *selected;
  return slot->capture_mesh_binding(node);
}

engine::error scene_instances::replace_mesh_binding(scene_instance_id instance, scene_node_id node,
                                                    const scene_mesh_binding& expected,
                                                    const scene_mesh_binding& replacement,
                                                    guid component_uuid) {
  const auto owner = native_world_.replace_mesh_renderers({});
  if (owner != engine::error::success) {
    return owner;
  }
  if ((replacement.mesh && !loader_.owns(replacement.mesh)) ||
      (replacement.material && !loader_.owns(replacement.material))) {
    return engine::error::invalid_argument;
  }
  auto selected = resolve(instance);
  if (!selected)
    return selected.error();
  auto* value = *selected;
  return value->replace_mesh_binding(node, expected, replacement, component_uuid);
}

engine::error scene_instances::set_mesh_renderer(scene_instance_id instance,
                                                 scene_node_id node_value,
                                                 std::string_view mesh_uri,
                                                 std::string_view material_uri) {
  auto node = node_value;
  {
    auto selected = resolve(instance);
    if (!selected)
      return selected.error();
    auto* value = *selected;
    return value->set_mesh_renderer(node, mesh_uri, material_uri);
  }
}

engine::error scene_instances::validate_editable_node(scene_instance_id instance,
                                                      scene_node_id node_value) const noexcept {
  auto node = node_value;
  auto selected = resolve(instance);
  if (!selected)
    return selected.error();
  auto* value = *selected;
  return value->contains_editable_node(scene_node_id{node}) ? engine::error::success
                                                            : engine::error::invalid_handle;
}

error scene_instances::add_component_description(scene_instance_id instance, scene_node_id node,
                                                 const component_description& description,
                                                 guid identity,
                                                 const scene_mesh_binding* prepared) {
  if (!native_world_.is_owner_thread())
    return error::invalid_state;
  auto selected = resolve(instance);
  if (!selected)
    return selected.error();
  auto* value = *selected;
  return value->add_component_description(node, description, identity, prepared);
}
error scene_instances::remove_component_description(scene_instance_id instance, scene_node_id node,
                                                    guid type) {
  if (!native_world_.is_owner_thread())
    return error::invalid_state;
  auto selected = resolve(instance);
  if (!selected)
    return selected.error();
  auto* value = *selected;
  return value->remove_component_description(node, type);
}
error scene_instances::set_component_description(scene_instance_id instance, scene_node_id node,
                                                 const component_description& description,
                                                 const scene_mesh_binding* prepared) {
  if (!native_world_.is_owner_thread())
    return error::invalid_state;
  auto selected = resolve(instance);
  if (!selected)
    return selected.error();
  auto* value = *selected;
  return value->set_component_description(node, description, prepared);
}
engine::error scene_instances::set_camera(scene_instance_id instance, scene_node_id node_value,
                                          const engine::camera_description& desc) {
  auto node = node_value;
  auto selected = resolve(instance);
  if (!selected)
    return selected.error();
  auto* value = *selected;
  return value->set_camera(node, desc);
}

engine::error scene_instances::remove_camera(scene_instance_id instance, scene_node_id node_value) {
  auto node = node_value;
  auto selected = resolve(instance);
  if (!selected)
    return selected.error();
  auto* value = *selected;
  return value->remove_camera(node);
}

engine::error scene_instances::remove_mesh_renderer(scene_instance_id instance,
                                                    scene_node_id node_value) {
  auto node = node_value;
  auto selected = resolve(instance);
  if (!selected)
    return selected.error();
  auto* value = *selected;
  return value->remove_mesh_renderer(node);
}

engine::error scene_instances::destroy_node(scene_instance_id instance, scene_node_id node_value) {
  auto node = node_value;
  {
    auto selected = resolve(instance);
    if (!selected)
      return selected.error();
    auto* value = *selected;
    return value->destroy_node(node);
  }
}

} // namespace gneiss::engine
