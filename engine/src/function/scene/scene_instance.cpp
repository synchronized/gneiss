// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/error.hpp>
#include <gneiss/engine/error.hpp>
#include <gneiss/engine/function/scene/scene_types.hpp>

#include "core/identifier_conversion.hpp"
#include "function/scene/detail/world_impl.hpp"
#include <gneiss/engine/function/scene/world.hpp>

#include <algorithm>
#include <cstdint>
#include <ranges>

namespace gneiss {
error world::validate_scene(scene_instance_id id) const noexcept {
  if (!is_owner_thread())
    return error::invalid_state;
  return impl_->scene_slots.get(id, resource_type::scene_instance) ? error::success
                                                                   : error::invalid_handle;
}
error world::unload_scene(scene_instance_id id) noexcept try {
  auto status = validate_scene(id);
  if (status.failed())
    return status;
  impl_->destroy_scene_members(id);
  return impl_->scene_slots.destroy(id, resource_type::scene_instance);
} catch (...) {
  fatal_current_exception();
}

result<scene_instance> scene_instance::borrow(world& owner, scene_instance_id id) noexcept {
  if (!owner.is_owner_thread())
    return error::invalid_state;
  auto count = owner.scene_size(id);
  if (!count)
    return count.error();
  scene_instance result;
  result.owner_ = &owner;
  result.id_ = id;
  return result;
}
bool scene_instance::is_valid() const noexcept { return size().has_value(); }
result<std::uint64_t> scene_instance::size() const noexcept {
  if (!owner_)
    return error::invalid_handle;
  return owner_->scene_size(id_);
}
result<std::vector<game_object>> scene_instance::objects() const {
  std::vector<game_object> objects;
  auto status = visit_objects([&](game_object object) {
    objects.push_back(object);
    return error::success;
  });
  return status.ok() ? result<std::vector<game_object>>{std::move(objects)}
                     : result<std::vector<game_object>>{status};
}
result<std::vector<game_object>> scene_instance::roots() const {
  std::vector<game_object> objects;
  auto status = visit_objects(
      [&](game_object object) {
        objects.push_back(object);
        return error::success;
      },
      true);
  return status.ok() ? result<std::vector<game_object>>{std::move(objects)}
                     : result<std::vector<game_object>>{status};
}
error scene_instance::visit_objects(const std::function<error(game_object)>& visitor,
                                    bool roots_only) const {
  if (!owner_)
    return error::invalid_handle;
  auto status = owner_->validate_scene(id_);
  if (status.failed())
    return status;
  if (!visitor)
    return error::invalid_argument;
  owner_->impl_->each_scene_member(id_, [&](entity_id node) {
    const auto* data = owner_->impl_->get<detail::object_state>(node.get());
    if (status.failed() || (roots_only && data && data->parent))
      return;
    auto object = owner_->object(entity_id{node.get()});
    if (!object && object.error() == error::not_found)
      return;
    if (!object) {
      status = object.error();
      return;
    }
    status = visitor(*object);
  });
  return status;
}
error world::transfer_subtree(game_object root, scene_instance destination, game_object parent) {
  if (!is_owner_thread())
    return error::invalid_state;
  if (root.owner() != this || destination.owner() != this ||
      (parent.owner() && parent.owner() != this))
    return error::invalid_handle;
  auto status = validate_scene(destination.id());
  if (status.failed())
    return status;
  auto source = root.scene();
  if (!source)
    return source.error();
  if (parent.owner()) {
    auto member = destination.contains(parent);
    if (!member)
      return member.error();
    if (!*member)
      return error::invalid_argument;
  }
  std::vector<entity_id> nodes{entity_id{root.entity().get()}};
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    if (parent.owner() && nodes[i].get() == parent.entity().get())
      return error::invalid_argument;
    if (impl_->scene_of(nodes[i]) != source->id())
      return error::invalid_state;
    const auto* object = impl_->get<detail::object_state>(nodes[i].get());
    const auto* children = object ? &object->children : nullptr;
    if (!children)
      return error::invalid_handle;
    nodes.insert(nodes.end(), children->begin(), children->end());
  }
  // 已有归属组件原位更新，不在提交阶段分配组件存储。
  auto assign = [&](scene_instance_id id) {
    for (auto node : nodes)
      impl_->assign_scene(node, id);
  };
  assign(destination.id());
  try {
    status = root.reparent(parent);
  } catch (...) {
    assign(source->id());
    throw;
  }
  if (status.failed())
    assign(source->id());
  return status;
}
result<bool> scene_instance::contains(game_object object) const noexcept {
  if (!owner_)
    return error::invalid_handle;
  auto status = owner_->validate_scene(id_);
  if (status.failed())
    return status;
  if (object.owner() != owner_)
    return false;
  return owner_->impl_->is_alive(object.entity().get()) &&
         owner_->impl_->scene_of(object.entity()) == id_;
}

error scene_instance::unload() const noexcept {
  if (!owner_)
    return error::invalid_handle;
  return owner_->unload_scene(id_);
}

result<std::uint64_t> world::scene_size(scene_instance_id instance) const {
  const auto status = validate_scene(instance);
  if (status.failed())
    return status;
  std::uint64_t count{};
  impl_->each_scene_member(instance, [&](entity_id) { ++count; });
  return count;
}

result<game_object> scene_instance::create_object(const object_options& options,
                                                  game_object parent) const {
  if (!owner_)
    return error::invalid_handle;
  auto status = owner_->validate_scene(id_);
  if (status.failed())
    return status;
  if (parent.owner()) {
    auto member = contains(parent);
    if (!member)
      return member.error();
    if (!*member)
      return error::invalid_handle;
  }
  // 创建实体可能移动层级组件；先复制可能借自已有对象的名称。
  std::string name(options.name);
  auto& state = *owner_->impl_;
  auto created = state.create_object(parent.entity());
  if (!created)
    return created.error();
  try {
    status = owner_->find(*created).set_local_transform(options.transform);
    if (status.ok())
      status = owner_->find(*created).set_name(name);
    if (status.failed()) {
      (void)state.destroy_entity(created->get());
      return status;
    }
    state.assign_scene(*created, id_);
  } catch (...) {
    (void)state.destroy_entity(created->get());
    throw;
  }
  return owner_->find(*created);
}
}
