// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "engine/core/rid.hpp"
#include "engine/core/scene_types.hpp"
#include "engine/function/scene/scene_instance.hpp"
#include "engine/function/scene/structural_diff.hpp"
#include <algorithm>
#include <new>
#include <type_traits>
#include <utility>

namespace gneiss::engine {
gneiss::engine::result scene_instance::capture_prefab_instance_header(
    std::string_view uuid, gneiss::engine::prefab_instance_description& author,
    gneiss::engine::prefab_asset_ref& source) const noexcept try {
  const auto found = std::ranges::find(description_.prefab_instances, uuid,
                                       &gneiss::engine::prefab_instance_description::instance_uuid);
  if (found == description_.prefab_instances.end()) {
    return gneiss::engine::result::not_found;
  }
  const auto index = static_cast<std::size_t>(found - description_.prefab_instances.begin());
  auto copy = *found;
  gneiss::engine::transform transform{};
  const auto queried = native_world_.node_get_local_transform(
      gneiss::scene_node_id{prefab_instances_[index]->root()}, transform);
  if (queried != gneiss::engine::result::success) {
    return queried;
  }
  copy.translation = transform.translation;
  copy.rotation = transform.rotation;
  copy.scale = transform.scale;
  source = prefab_instances_[index]->prefab_lease();
  author = std::move(copy);
  return gneiss::engine::result::success;
} catch (const std::bad_alloc&) {
  return gneiss::engine::result::out_of_memory;
} catch (...) {
  return gneiss::engine::result::internal;
}

gneiss::engine::result
scene_instance::capture_prefab_instance_binding(std::string_view uuid, std::size_t index,
                                                scene_mesh_binding& output) const noexcept {
  const auto found = std::ranges::find_if(
      prefab_instances_, [&](const auto& value) { return value->instance_uuid_ == uuid; });
  if (found == prefab_instances_.end()) {
    return gneiss::engine::result::not_found;
  }
  if (index >= (*found)->nodes_.size()) {
    return gneiss::engine::result::not_found;
  }
  const auto& node = (*found)->nodes_[index];
  output = {
      .mesh = node.mesh,
      .material = node.material,
  };
  return gneiss::engine::result::success;
}

result scene_instance::instantiate_prefab_ready(const prefab_instance_description& author,
                                                prefab_asset_ref prefab,
                                                std::span<const scene_mesh_binding> bindings,
                                                bool replacing, scene_node_id& output) noexcept
    try {
  output = {};
  if (!prefab || bindings.size() != prefab.get()->objects.size() ||
      !is_canonical_prefab_uuid(author.instance_uuid) || find_node(author.instance_uuid))
    return result::invalid_argument;
  const auto previous = std::ranges::find_if(prefab_instances_, [&](const auto& value) {
    return value->instance_uuid_ == author.instance_uuid;
  });
  if (replacing ? previous == prefab_instances_.end() : previous != prefab_instances_.end())
    return result::invalid_argument;
  const auto parent = author.parent_uuid ? find_node(*author.parent_uuid) : scene_node_id{};
  if (author.parent_uuid && !parent)
    return result::invalid_handle;
  // 基础输入校验完成后直接移除旧实例，不保留运行时副本。
  if (replacing) {
    const auto removed = remove_prefab_instance(author.instance_uuid);
    if (removed.failed())
      return removed;
  }
  auto record = author;
  prefab_description effective;
  auto status = prefab_runtime_instance::prepare_description(
      *prefab.get(), registry_, author.instance_uuid, author.overrides, effective);
  if (status.failed())
    return status;
  structural_diff order;
  status = build_structural_diff({}, effective.objects, order);
  if (status.failed())
    return status;
  description_.prefab_instances.reserve(description_.prefab_instances.size() + 1U);
  prefab_instances_.reserve(prefab_instances_.size() + 1U);
  auto runtime = std::make_unique<prefab_runtime_instance>(native_world_, loader_,
                                                           std::move(prefab), author.instance_uuid);
  runtime->prepared_ = std::move(effective);
  status = runtime->create_root(parent, {author.translation, author.rotation, author.scale});
  if (status.failed())
    return status;
  for (const auto& entry : order.added) {
    const auto index = entry.new_index;
    const auto& source = runtime->prepared_->objects[index];
    const auto node_parent =
        source.parent_uuid ? runtime->find_node(*source.parent_uuid) : runtime->root();
    status =
        runtime->create_node(index, node_parent, bindings[index].mesh, bindings[index].material);
    if (status.failed())
      return status;
  }
  runtime->prepared_.reset();
  const auto root = runtime->root();
  description_.prefab_instances.push_back(std::move(record));
  prefab_instances_.push_back(std::move(runtime));
  output = root;
  return result::success;
} catch (const std::bad_alloc&) {
  return result::out_of_memory;
} catch (...) {
  return result::internal;
}

result scene_instance::remove_prefab_instance(std::string_view uuid) noexcept {
  const auto found = std::ranges::find_if(
      prefab_instances_, [&](const auto& value) { return value->instance_uuid_ == uuid; });
  if (found == prefab_instances_.end())
    return result::not_found;
  const auto index = static_cast<std::size_t>(found - prefab_instances_.begin());
  prefab_instances_.erase(found);
  description_.prefab_instances.erase(description_.prefab_instances.begin() +
                                      static_cast<std::ptrdiff_t>(index));
  return result::success;
}
} // namespace gneiss::engine
