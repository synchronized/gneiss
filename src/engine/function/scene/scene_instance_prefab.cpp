// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/error.hpp"
#include "engine/core/rid.hpp"
#include "engine/core/scene_types.hpp"
#include "engine/function/scene/scene_instance.hpp"
#include "engine/function/scene/structural_diff.hpp"
#include <algorithm>
#include <new>
#include <type_traits>
#include <utility>

namespace gneiss::engine {
error scene_instance::capture_prefab_instance_header(gneiss::engine::guid uuid,
                                                     object_description& author,
                                                     prefab_asset_ref& source) const noexcept try {
  const auto found = std::ranges::find_if(
      prefab_instances_, [&](const auto& value) { return value->instance_uuid_ == uuid; });
  if (found == prefab_instances_.end())
    return error::not_found;
  const auto index = static_cast<std::size_t>(found - prefab_instances_.begin());
  auto copy = prefab_author(index);
  math::transform transform{};
  const auto queried = native_world_.node_get_local_transform(
      gneiss::scene_node_id{prefab_instances_[index]->root()}, transform);
  if (queried != error::success) {
    return queried;
  }
  copy.transform = transform;
  source = prefab_instances_[index]->prefab_lease();
  author = std::move(copy);
  return error::success;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}

error scene_instance::capture_prefab_instance_binding(gneiss::engine::guid uuid, std::size_t index,
                                                      scene_mesh_binding& output) const noexcept {
  const auto found = std::ranges::find_if(
      prefab_instances_, [&](const auto& value) { return value->instance_uuid_ == uuid; });
  if (found == prefab_instances_.end()) {
    return error::not_found;
  }
  if (index >= (*found)->nodes_.size()) {
    return error::not_found;
  }
  const auto& node = (*found)->nodes_[index];
  auto renderer = native_world_.node_mesh_renderer(node.node);
  if (!renderer) {
    output = {};
    return renderer.error() == error::not_found ? error::success : renderer.error();
  }
  output = {.mesh = renderer->mesh, .material = renderer->material};
  return error::success;
}

error scene_instance::instantiate_prefab_ready(const object_description& author,
                                               prefab_asset_ref prefab,
                                               std::span<const scene_mesh_binding> bindings,
                                               bool replacing, scene_node_id& output) noexcept try {
  output = {};
  if (!author.prefab || !author.components.empty() || !prefab ||
      bindings.size() != prefab.get()->objects.size() || !author.uuid ||
      (!replacing && find_node(author.uuid)))
    return error::invalid_argument;
  const auto previous = std::ranges::find_if(
      prefab_instances_, [&](const auto& value) { return value->instance_uuid_ == author.uuid; });
  if (replacing ? previous == prefab_instances_.end() : previous != prefab_instances_.end())
    return error::invalid_argument;
  const auto parent = author.parent_uuid ? find_node(*author.parent_uuid) : scene_node_id{};
  if (author.parent_uuid && !parent)
    return error::invalid_handle;
  // 自有子对象不属于模板；刷新时保留其身份和局部变换。
  std::vector<scene_node_id> attached;
  if (replacing) {
    for (std::size_t index = 0; index < description_.objects.size(); ++index)
      if (description_.objects[index].parent_uuid == author.uuid)
        attached.push_back(objects_[index].node);
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
  auto record = author;
  prefab_description effective;
  auto status =
      prepare_prefab_description(*prefab.get(), registry_, author.prefab->overrides, effective);
  if (status.failed())
    return status;
  structural_diff order;
  status = build_structural_diff({}, effective.objects, order);
  if (status.failed())
    return status;
  description_.objects.reserve(description_.objects.size() + 1U);
  prefab_instances_.reserve(prefab_instances_.size() + 1U);
  auto runtime =
      std::make_unique<prefab_binding>(native_world_, loader_, std::move(prefab), author.uuid);
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
        source.parent_uuid ? runtime->find_node(*source.parent_uuid) : runtime->root();
    status = runtime->create_node(index, source, node_parent, bindings[index].mesh,
                                  bindings[index].material);
    if (status.failed())
      return status;
  }
  const auto root = runtime->root();
  objects_.reserve(objects_.size() + runtime->node_count() + 1U);
  objects_.insert(objects_.begin() + static_cast<std::ptrdiff_t>(description_.objects.size()),
                  {root});
  description_.objects.push_back(std::move(record));
  append_staged_prefab(std::move(runtime));
  guard.release();
  for (const auto node : attached) {
    const auto restored = reparent_node(node, root);
    if (restored.failed())
      return restored;
  }
  output = root;
  return error::success;
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
