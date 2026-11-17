// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/assert.hpp>
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
  auto status = owner.validate_scene(id);
  if (status.failed())
    return status;
  scene_instance result;
  result.owner_ = &owner;
  result.id_ = id;
  return result;
}
bool scene_instance::is_valid() const noexcept {
  return owner_ && owner_->validate_scene(id_).ok();
}
std::uint64_t scene_instance::size() const noexcept {
  GNEISS_ASSERT(is_valid(), "场景查询要求有效句柄及所属线程");
  std::uint64_t count{};
  owner_->impl_->each_scene_member(id_, [&](entity_id) { ++count; });
  return count;
}
std::vector<game_object> scene_instance::objects() const {
  GNEISS_ASSERT(is_valid(), "场景查询要求有效句柄及所属线程");
  std::vector<game_object> objects;
  owner_->impl_->each_scene_member(id_, [&](entity_id id) {
    if (owner_->impl_->get<detail::object_state>(id.get()))
      objects.push_back(owner_->find(id));
  });
  return objects;
}
std::vector<game_object> scene_instance::roots() const {
  GNEISS_ASSERT(is_valid(), "场景查询要求有效句柄及所属线程");
  std::vector<game_object> objects;
  owner_->impl_->each_scene_member(id_, [&](entity_id id) {
    const auto* object = owner_->impl_->get<detail::object_state>(id.get());
    if (object && !object->parent)
      objects.push_back(owner_->find(id));
  });
  return objects;
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
  if (!root.is_valid())
    return error::invalid_handle;
  const auto source = root.scene();
  if (parent.owner()) {
    if (!destination.contains(parent))
      return error::invalid_argument;
  }
  std::vector<entity_id> nodes{entity_id{root.entity().get()}};
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    if (parent.owner() && nodes[i].get() == parent.entity().get())
      return error::invalid_argument;
    if (impl_->scene_of(nodes[i]) != source.id())
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
    assign(source.id());
    throw;
  }
  if (status.failed())
    assign(source.id());
  return status;
}
bool scene_instance::contains(game_object object) const noexcept {
  GNEISS_ASSERT(is_valid(), "场景查询要求有效句柄及所属线程");
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

result<game_object> scene_instance::create_object(const object_options& options,
                                                  game_object parent) const {
  if (!owner_)
    return error::invalid_handle;
  auto status = owner_->validate_scene(id_);
  if (status.failed())
    return status;
  if (parent.owner()) {
    if (!contains(parent))
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
