// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/error.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/scene/prefab_replay.hpp"
#include "engine/function/scene/scene_instance.hpp"
#include "engine/function/scene/scene_types.hpp"
#include "engine/function/scene/structural_diff.hpp"
#include <algorithm>
#include <new>
#include <type_traits>
#include <utility>

namespace gneiss::engine {
result<scene_node_id> scene_instance::instantiate_prefab_ready(
    const game_object_description& author, prefab_asset_ref prefab,
    std::span<const scene_mesh_binding> bindings, bool replacing,
    ref<const engine::prefab> source_asset, prefab_replacement mode) noexcept try {
  if (!author.prefab || !author.components.empty() || !prefab ||
      bindings.size() != prefab.get()->objects.size() || !author.uuid ||
      (!replacing && find_node(author.uuid)))
    return error::invalid_argument;
  const auto previous = std::ranges::find_if(
      prefab_instances_, [&](const auto& value) { return value->instance_uuid_ == author.uuid; });
  if (replacing ? previous == prefab_instances_.end() : previous != prefab_instances_.end())
    return error::invalid_argument;
  const auto parent = author.parent_uuid ? find_node(author.parent_uuid) : scene_node_id{};
  if (author.parent_uuid && !parent)
    return error::invalid_handle;
  auto replayed = replay_prefab_reference(*prefab, *author.prefab);
  if (!replayed)
    return replayed.error();
  const auto& effective = replayed->description;
  // 相同来源根按 UUID 增量更新，保留存活节点及外接子对象。
  if (replacing && mode == prefab_replacement::preserve_nodes &&
      (*previous)->prefab_->uuid == prefab->uuid &&
      (*previous)->prefab_->root_uuid == prefab->root_uuid) {
    auto& runtime = **previous;
    objects_.reserve(objects_.size() + prefab->objects.size());
    std::vector<scene_node_id> members;
    for (const auto& member : runtime.nodes_)
      if (member.node != runtime.root())
        members.push_back(member.node);
    auto status = runtime.reload(prefab, effective);
    if (status.failed())
      return status;
    std::erase_if(objects_, [&](const auto& object) {
      return std::ranges::find(members, object.node) != members.end();
    });
    append_prefab_members(runtime);
    runtime.reference_ = *author.prefab;
    runtime.source_asset_ = std::move(source_asset);
    status = native_world_.node_reparent(runtime.root(), parent);
    if (status.ok())
      status = native_world_.node_set_identity(runtime.root(), {author.uuid, author.name});
    if (status.ok())
      status = native_world_.node_set_local_transform(runtime.root(), author.transform);
    if (status.failed())
      return status;
    for (std::size_t index = 0; index < prefab->objects.size(); ++index) {
      const auto node = runtime.find_node(prefab->objects[index].uuid);
      auto current = capture_mesh_binding(node);
      if (!current) {
        if (current.error() == error::not_found)
          continue;
        return current.error();
      }
      status = replace_mesh_binding(node, *current, bindings[index]);
      if (status.failed())
        return status;
    }
    return runtime.root();
  }
  // 自有子对象不属于模板；刷新时保留其身份和局部变换。
  std::vector<scene_node_id> attached;
  if (replacing) {
    for (std::size_t index = 0; index < objects_.size(); ++index) {
      if (!objects_[index].persistent)
        continue;
      scene_node_id actual_parent;
      const auto status = native_world_.node_get_parent(objects_[index].node, actual_parent);
      if (status.failed())
        return status;
      if (actual_parent == (*previous)->root())
        attached.push_back(objects_[index].node);
    }
    for (const auto node : attached) {
      const auto detached = reparent_node(node, parent);
      if (detached.failed())
        return detached;
    }
  }
  // 失败不恢复旧实例；已分离的自有子对象保留在原实例的父节点下。
  if (replacing) {
    const auto removed = remove_prefab_instance(author.uuid);
    if (removed.failed())
      return removed;
  }
  auto reference = *author.prefab;
  auto status = error{error::success};
  structural_diff order;
  status = build_structural_diff({}, effective.objects, order);
  if (status.failed())
    return status;
  prefab_instances_.reserve(prefab_instances_.size() + 1U);
  auto runtime = std::make_unique<prefab_binding>(native_world_, loader_, std::move(prefab),
                                                  author.uuid, std::move(source_asset));
  status = runtime->create_root(parent, author.transform);
  if (status.failed())
    return status;
  scene_subtree_guard guard(native_world_, runtime->root());
  status = native_world_.node_set_identity(runtime->root(), {author.uuid, author.name});
  if (status.failed())
    return status;
  for (const auto& entry : order.added) {
    const auto index = entry.new_index;
    const auto& source = effective.objects[index];
    const auto node_parent =
        source.parent_uuid ? runtime->find_node(source.parent_uuid) : runtime->root();
    auto binding = bindings[index];
    status = runtime->create_node(index, source, node_parent, binding.mesh, binding.material);
    if (status.failed())
      return status;
  }
  const auto root = runtime->root();
  objects_.reserve(objects_.size() + runtime->node_count() + 1U);
  const auto member_index = objects_.size();
  objects_.push_back({root});
  append_staged_prefab(std::move(runtime), std::move(reference), member_index);
  guard.release();
  for (const auto node : attached) {
    const auto restored = reparent_node(node, root);
    if (restored.failed())
      return restored;
  }
  return root;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}

error scene_instance::remove_prefab_instance(gneiss::engine::guid uuid) noexcept {
  const auto found = std::ranges::find_if(
      prefab_instances_, [&](const auto& value) { return value->instance_uuid_ == uuid; });
  if (found == prefab_instances_.end())
    return error::not_found;
  return destroy_subtree((*found)->root());
}
} // namespace gneiss::engine
