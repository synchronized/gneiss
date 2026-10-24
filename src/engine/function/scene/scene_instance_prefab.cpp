// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "engine/function/scene/scene_instance.hpp"
#include <algorithm>
#include <new>
#include <type_traits>
#include <utility>

namespace gneiss::engine::scene {
void scene_instance::append_render_assets(
    std::vector<engine::render::render_asset_lease>& output) const {
  const auto append = [&](const auto& node) {
    if (node.mesh) {
      output.push_back(engine::render::render_asset_loader::asset_lease(node.mesh));
    }
    if (node.material) {
      output.push_back(engine::render::render_asset_loader::asset_lease(node.material));
    }
  };
  for (const auto& node : objects_) {
    append(node);
  }
  for (const auto& prefab : prefab_instances_) {
    for (const auto& node : prefab->nodes_) {
      append(node);
    }
  }
}

gneiss::engine::core::result scene_instance::capture_prefab_instance_header(
    std::string_view uuid, gneiss::engine::resource::prefab_instance_description& author,
    gneiss::engine::resource::prefab_asset_lease& source) const noexcept try {
  const auto found =
      std::ranges::find(description_.prefab_instances, uuid,
                        &gneiss::engine::resource::prefab_instance_description::instance_uuid);
  if (found == description_.prefab_instances.end()) {
    return gneiss::engine::core::result::not_found;
  }
  const auto index = static_cast<std::size_t>(found - description_.prefab_instances.begin());
  auto copy = *found;
  gneiss::engine::core::math::transform transform{};
  const auto queried = native_world_.node_get_local_transform(
      gneiss::scene_node_id{prefab_instances_[index]->root()}, transform);
  if (queried != gneiss::engine::core::result::success) {
    return queried;
  }
  std::ranges::copy(transform.translation, copy.translation.begin());
  std::ranges::copy(transform.rotation, copy.rotation.begin());
  std::ranges::copy(transform.scale, copy.scale.begin());
  source = prefab_instances_[index]->prefab_lease();
  author = std::move(copy);
  return gneiss::engine::core::result::success;
} catch (const std::bad_alloc&) {
  return gneiss::engine::core::result::out_of_memory;
} catch (...) {
  return gneiss::engine::core::result::internal;
}

gneiss::engine::core::result
scene_instance::capture_prefab_instance_binding(std::string_view uuid, std::size_t index,
                                                scene_mesh_binding& output) const noexcept {
  const auto found = std::ranges::find_if(
      prefab_instances_, [&](const auto& value) { return value->instance_uuid_ == uuid; });
  if (found == prefab_instances_.end()) {
    return gneiss::engine::core::result::not_found;
  }
  if (index >= (*found)->nodes_.size()) {
    return gneiss::engine::core::result::not_found;
  }
  const auto& node = (*found)->nodes_[index];
  output = {
      .mesh = engine::render::render_asset_loader::asset_lease(node.mesh),
      .material = engine::render::render_asset_loader::asset_lease(node.material),
  };
  return gneiss::engine::core::result::success;
}

gneiss::engine::core::result scene_instance::prepare_prefab_instance_candidate(
    gneiss_scene_instance instance,
    const gneiss::engine::resource::prefab_instance_description& author,
    gneiss::engine::resource::prefab_asset_lease prefab,
    const gneiss::engine::resource::prefab_description& prepared, prefab_candidate& output,
    bool replacing) noexcept try {
  if (output.is_valid()) {
    return gneiss::engine::core::result::invalid_state;
  }
  output.author_ = author;
  const auto previous = std::ranges::find_if(prefab_instances_, [&](const auto& value) {
    return value->instance_uuid_ == author.instance_uuid;
  });
  if (!gneiss::engine::resource::is_canonical_prefab_uuid(author.instance_uuid) ||
      find_node(author.instance_uuid) != GNEISS_NULL_SCENE_NODE_ID ||
      (replacing ? previous == prefab_instances_.end() : previous != prefab_instances_.end())) {
    return gneiss::engine::core::result::invalid_argument;
  }
  const auto parent =
      author.parent_uuid ? find_node(*author.parent_uuid) : GNEISS_NULL_SCENE_NODE_ID;
  if (author.parent_uuid && parent == GNEISS_NULL_SCENE_NODE_ID) {
    return gneiss::engine::core::result::invalid_handle;
  }
  gneiss::engine::resource::prefab_description effective;
  const auto validation = prefab_runtime_instance::prepare_candidate_description(
      prepared, registry_, author.instance_uuid, author.overrides, effective);
  if (validation != gneiss::engine::core::result::success) {
    return validation;
  }
  const auto previous_root = replacing ? (*previous)->root() : 0U;
  description_.prefab_instances.reserve(description_.prefab_instances.size() + 1U);
  prefab_instances_.reserve(prefab_instances_.size() + 1U);
  auto candidate = std::make_unique<prefab_runtime_instance>(
      native_world_, reflection_context_, loader_, std::move(prefab), author.instance_uuid);
  gneiss::engine::core::math::transform transform = {};
  std::ranges::copy(author.translation, transform.translation.begin());
  std::ranges::copy(author.rotation, transform.rotation.begin());
  std::ranges::copy(author.scale, transform.scale.begin());
  const auto result = candidate->begin_hidden(parent, transform, std::move(effective));
  candidate->candidate_scene_ = instance;
  candidate->candidate_previous_root_ = previous_root;
  // 即使根创建失败也交还候选，由协调者分步清理已分配的记录。
  output.runtime_ = std::move(candidate);
  return result;
} catch (const std::bad_alloc&) {
  return gneiss::engine::core::result::out_of_memory;
} catch (...) {
  return gneiss::engine::core::result::internal;
}

// 已验证容量；两次 push_back 不分配，元素移动由编译断言保证不抛出。
// NOLINTBEGIN(bugprone-exception-escape): 分析器无法关联容量检查与 vector::push_back。
gneiss::engine::core::result
scene_instance::publish_prefab_instance_candidate(gneiss_scene_instance instance,
                                                  prefab_candidate& candidate) noexcept {
  auto& author = candidate.author_;
  auto& runtime = candidate.runtime_;
  if (!runtime || &runtime->native_world_ != &native_world_ ||
      runtime->candidate_scene_ != instance || runtime->instance_uuid_ != author.instance_uuid) {
    return gneiss::engine::core::result::invalid_handle;
  }
  if (find_node(author.instance_uuid) != GNEISS_NULL_SCENE_NODE_ID ||
      description_.prefab_instances.size() == description_.prefab_instances.capacity() ||
      prefab_instances_.size() == prefab_instances_.capacity() ||
      std::ranges::any_of(description_.prefab_instances, [&](const auto& existing) {
        return existing.instance_uuid == author.instance_uuid;
      })) {
    return gneiss::engine::core::result::invalid_state;
  }
  static_assert(
      std::is_nothrow_move_constructible_v<gneiss::engine::resource::prefab_instance_description>);
  static_assert(std::is_nothrow_move_constructible_v<std::unique_ptr<prefab_runtime_instance>>);
  const auto result = runtime->publish_hidden();
  if (result != gneiss::engine::core::result::success) {
    return result;
  }
  description_.prefab_instances.push_back(std::move(author));
  prefab_instances_.push_back(std::move(runtime));
  return gneiss::engine::core::result::success;
}

// NOLINTEND(bugprone-exception-escape)

gneiss::engine::core::result scene_instance::replace_prefab_instance_versions(
    gneiss_scene_instance instance, std::span<prefab_candidate* const> replacements) noexcept try {
  if (replacements.empty()) {
    return gneiss::engine::core::result::invalid_argument;
  }
  std::vector<std::size_t> indices;
  std::vector<engine::core::publication> previous;
  std::vector<engine::core::publication> candidates;
  indices.reserve(replacements.size());
  previous.reserve(replacements.size());
  candidates.reserve(replacements.size());
  for (const auto& entry : replacements) {
    if (entry == nullptr || !entry->runtime_) {
      return gneiss::engine::core::result::invalid_argument;
    }
    const auto& candidate = *entry->runtime_;
    if (&candidate.native_world_ != &native_world_ || candidate.candidate_scene_ != instance ||
        candidate.instance_uuid_ != entry->author_.instance_uuid || !candidate.prepared_ ||
        candidate.root_node_ == GNEISS_NULL_SCENE_NODE_ID || candidate.published_ ||
        candidate.cancelling_ ||
        std::ranges::any_of(candidate.nodes_, [](const auto& node) { return node.node == 0U; })) {
      return gneiss::engine::core::result::invalid_state;
    }
    const auto found = std::ranges::find_if(prefab_instances_, [&](const auto& value) {
      return value->instance_uuid_ == candidate.instance_uuid_;
    });
    if (found == prefab_instances_.end() ||
        (*found)->root() != candidate.candidate_previous_root_) {
      return gneiss::engine::core::result::invalid_state;
    }
    indices.push_back(static_cast<std::size_t>(found - prefab_instances_.begin()));
    previous.push_back((*found)->publication_);
    candidates.push_back(candidate.publication_);
  }
  const auto result = native_world_.replace_publications(previous, candidates);
  if (result != gneiss::engine::core::result::success) {
    return result;
  }
  // 可失败操作全部在上面结束；交换作者记录与独占所有权，不分配、不派发外部回调。
  for (std::size_t index = 0; index < replacements.size(); ++index) {
    const auto& entry = replacements[index];
    auto& old = prefab_instances_[indices[index]];
    old->published_ = false;
    old->cancelling_ = true;
    auto& candidate = *entry->runtime_;
    candidate.published_ = true;
    for (std::size_t node = 0; node < candidate.nodes_.size(); ++node) {
      const auto& camera = candidate.candidate_description().objects[node].camera;
      if (camera && camera->is_primary) {
        (void)native_world_.set_active_camera(gneiss::entity_id{candidate.nodes_[node].entity});
      }
    }
    std::swap(description_.prefab_instances[indices[index]], entry->author_);
    old.swap(entry->runtime_);
  }
  return gneiss::engine::core::result::success;
} catch (const std::bad_alloc&) {
  return gneiss::engine::core::result::out_of_memory;
} catch (...) {
  return gneiss::engine::core::result::internal;
}

gneiss::engine::core::result
scene_instance::prepare_prefab_instance_removal(std::string_view uuid, bool& complete) noexcept
    try {
  complete = false;
  const auto found = std::ranges::find_if(
      prefab_instances_, [&](const auto& value) { return value->instance_uuid_ == uuid; });
  return found == prefab_instances_.end() ? gneiss::engine::core::result::not_found
                                          : (*found)->prepare_retirement_step(complete);
} catch (const std::bad_alloc&) {
  return gneiss::engine::core::result::out_of_memory;
} catch (...) {
  return gneiss::engine::core::result::internal;
}

gneiss::engine::core::result
scene_instance::remove_prefab_instance(std::string_view uuid,
                                       prefab_candidate& candidate) noexcept {
  auto& retired = candidate.runtime_;
  if (retired) {
    return gneiss::engine::core::result::invalid_state;
  }
  const auto found = std::ranges::find_if(
      prefab_instances_, [&](const auto& value) { return value->instance_uuid_ == uuid; });
  if (found == prefab_instances_.end()) {
    return gneiss::engine::core::result::not_found;
  }
  const auto result = (*found)->retire_hidden();
  if (result != gneiss::engine::core::result::success) {
    return result;
  }
  const auto index = static_cast<std::size_t>(found - prefab_instances_.begin());
  retired = std::move(*found);
  prefab_instances_.erase(found);
  description_.prefab_instances.erase(description_.prefab_instances.begin() +
                                      static_cast<std::ptrdiff_t>(index));
  return gneiss::engine::core::result::success;
}

} // namespace gneiss::engine::scene
