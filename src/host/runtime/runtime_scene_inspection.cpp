// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_scene_inspection.hpp"
#include "engine/core/error.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/scene/scene_types.hpp"
#include "engine/function/scene/component_adapter.hpp"
#include "engine/function/scene/scene_instances.hpp"
#include "engine/function/world/world.hpp"
#include "runtime/application.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <new>
#include <set>
#include <string_view>
#include <utility>

namespace {

bool equal_transform(const gneiss::engine::math::transform& left,
                     const gneiss::engine::math::transform& right) noexcept {
  return (left.translation == right.translation) && (left.rotation == right.rotation) &&
         (left.scale == right.scale);
}

bool equal_node(const gneiss::host::runtime_scene_snapshot_node& left,
                const gneiss::host::runtime_scene_snapshot_node& right) {
  return left.id == right.id && left.parent == right.parent && left.uuid == right.uuid &&
         left.prefab_instance_uuid == right.prefab_instance_uuid &&
         left.prefab_source_node_uuid == right.prefab_source_node_uuid && left.name == right.name &&
         equal_transform(left.local_transform, right.local_transform) &&
         gneiss::engine::equal_components(left.components, right.components);
}

std::array<gneiss::engine::guid, 3>
author_key(const gneiss::host::runtime_scene_source_node& node) {
  if (node.prefab_instance_uuid.is_nil()) {
    return {node.uuid, {}, {}};
  }
  return {gneiss::engine::guid{}, node.prefab_instance_uuid, node.prefab_source_node_uuid};
}

} // namespace

namespace gneiss::host {

runtime_scene_inspection::runtime_scene_inspection(std::uint64_t session_id) noexcept
    : session_id_(session_id) {}

void runtime_scene_inspection::reset(std::uint64_t session_id) noexcept {
  session_id_ = session_id;
  next_sequence_ = 1U;
  next_object_value_ = 1U;
  identities_.clear();
  generations_.clear();
  free_values_.clear();
  previous_.clear();
  entities_.clear();
  is_initialized_ = false;
}

gneiss::engine::error
runtime_scene_inspection::capture(std::span<const runtime_scene_source_node> nodes, bool force_full,
                                  runtime_scene_snapshot& output) noexcept {
  if (session_id_ == 0U || nodes.size() > runtime_inspection_max_nodes) {
    return gneiss::engine::error::invalid_argument;
  }
  try {
    std::set<std::array<gneiss::engine::guid, 3>> author_keys;
    std::set<std::uint64_t> native_nodes;
    for (const auto& node : nodes) {
      if (node.native_node == 0U || node.uuid.is_nil() ||
          (!node.prefab_source_node_uuid.is_nil() && node.prefab_instance_uuid.is_nil()) ||
          node.name.size() > runtime_inspection_max_string_size ||
          !author_keys.insert(author_key(node)).second ||
          !native_nodes.insert(node.native_node).second) {
        return gneiss::engine::error::invalid_argument;
      }
    }

    auto identities = identities_;
    auto generations = generations_;
    auto free_values = free_values_;
    auto next_object_value = next_object_value_;
    for (auto iterator = identities.begin(); iterator != identities.end();) {
      if (author_keys.contains(iterator->first)) {
        ++iterator;
        continue;
      }
      const auto id = iterator->second;
      auto& generation = generations[id.value];
      if (generation < std::numeric_limits<std::uint32_t>::max()) {
        ++generation;
        free_values.push_back(id.value);
      }
      iterator = identities.erase(iterator);
    }

    for (const auto& node : nodes) {
      const auto key = author_key(node);
      if (identities.contains(key)) {
        continue;
      }
      std::uint64_t value = 0U;
      if (!free_values.empty()) {
        value = free_values.back();
        free_values.pop_back();
      } else {
        if (next_object_value == 0U) {
          return gneiss::engine::error::out_of_memory;
        }
        value = next_object_value++;
        generations.emplace(value, 1U);
      }
      identities.emplace(key, ipc_runtime_object_id{value, generations.at(value)});
    }

    std::map<std::uint64_t, ipc_runtime_object_id> native_identities;
    for (const auto& node : nodes) {
      native_identities.emplace(node.native_node, identities.at(author_key(node)));
    }

    std::map<std::uint64_t, runtime_scene_snapshot_node> current;
    std::map<std::uint64_t, std::pair<ipc_runtime_object_id, std::uint64_t>> entities;
    std::vector<runtime_scene_snapshot_node> ordered;
    ordered.reserve(nodes.size());
    for (const auto& source : nodes) {
      ipc_runtime_object_id parent;
      if (source.native_parent != 0U) {
        const auto found = native_identities.find(source.native_parent);
        if (found == native_identities.end()) {
          return gneiss::engine::error::invalid_argument;
        }
        parent = found->second;
      }
      runtime_scene_snapshot_node node{.id = identities.at(author_key(source)),
                                       .parent = parent,
                                       .uuid = source.uuid,
                                       .prefab_instance_uuid = source.prefab_instance_uuid,
                                       .prefab_source_node_uuid = source.prefab_source_node_uuid,
                                       .name = source.name,
                                       .local_transform = source.local_transform,
                                       .components = source.components};
      current.emplace(node.id.value, node);
      if (source.native_entity != UINT64_C(0)) {
        entities.emplace(node.id.value, std::pair{node.id, source.native_entity});
      }
      ordered.push_back(std::move(node));
    }

    runtime_scene_snapshot pending;
    pending.is_full = force_full || !is_initialized_;
    if (!pending.is_full) {
      for (const auto& [value, previous] : previous_) {
        if (!current.contains(value) || current.at(value).id != previous.id) {
          pending.changes.push_back(
              {.type = runtime_scene_change_type::remove, .id = previous.id, .node = {}});
        }
      }
    }
    for (auto& node : ordered) {
      const auto previous = previous_.find(node.id.value);
      if (pending.is_full || previous == previous_.end() || !equal_node(previous->second, node)) {
        pending.changes.push_back(
            {.type = runtime_scene_change_type::upsert, .id = node.id, .node = std::move(node)});
      }
    }
    if (pending.changes.size() > runtime_inspection_max_changes) {
      return gneiss::engine::error::out_of_memory;
    }
    if (pending.changes.empty() && !pending.is_full) {
      output = {};
      return gneiss::engine::error::not_ready;
    }
    if (next_sequence_ == 0U) {
      return gneiss::engine::error::out_of_memory;
    }
    pending.stamp = {.session_id = session_id_, .sequence = next_sequence_++};
    identities_ = std::move(identities);
    generations_ = std::move(generations);
    free_values_ = std::move(free_values);
    next_object_value_ = next_object_value;
    previous_ = std::move(current);
    entities_ = std::move(entities);
    is_initialized_ = true;
    output = std::move(pending);
    return gneiss::engine::error::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::error::out_of_memory;
  } catch (...) {
    return gneiss::engine::error::internal;
  }
}

gneiss::engine::error
runtime_scene_inspection::capture_scene(::gneiss::runtime::application& application,
                                        gneiss::scene_instance_id scene, bool force_full,
                                        runtime_scene_snapshot& output) noexcept {
  if (!application.is_owner_thread() || !application.accepts_requests())
    return gneiss::engine::error::invalid_state;
  if (!scene.is_valid()) {
    return gneiss::engine::error::invalid_argument;
  }
  std::uint64_t count = 0U;
  auto operation = application.scenes()->get_node_count(scene, &count);
  if (operation != gneiss::engine::error::success) {
    return operation;
  }
  if (count > runtime_inspection_max_nodes) {
    return gneiss::engine::error::out_of_memory;
  }
  auto& world = *application.native_world();
  try {
    std::vector<runtime_scene_source_node> nodes;
    nodes.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t index = 0U; index < count; ++index) {
      gneiss::engine::scene_node_info info;
      operation = application.scenes()->get_node_info(scene, index, info);
      if (operation != gneiss::engine::error::success) {
        return operation;
      }
      if (info.name.size() > runtime_inspection_max_string_size)
        return gneiss::engine::error::invalid_argument;
      if (info.prefab && (info.prefab->instance_uuid.is_nil()))
        return gneiss::engine::error::invalid_argument;
      auto runtime_transform = info.local_transform;
      operation =
          world.entity_get_local_transform(gneiss::entity_id{info.entity.get()}, runtime_transform);
      if (operation != gneiss::engine::error::success) {
        return operation;
      }
      nodes.push_back({.native_node = info.node.get(),
                       .native_parent = info.parent.get(),
                       .native_entity = info.entity.get(),
                       .uuid = info.uuid,
                       .prefab_instance_uuid =
                           info.prefab ? info.prefab->instance_uuid : gneiss::engine::guid{},
                       .prefab_source_node_uuid =
                           info.prefab ? info.prefab->source_node_uuid : gneiss::engine::guid{},
                       .name = std::string{info.name},
                       .local_transform = runtime_transform,
                       .components = std::move(info.components)});
    }
    return capture(nodes, force_full, output);
  } catch (const std::bad_alloc&) {
    return gneiss::engine::error::out_of_memory;
  } catch (...) {
    return gneiss::engine::error::internal;
  }
}

gneiss::engine::error
runtime_scene_inspection::resolve_entity(ipc_runtime_object_id object,
                                         std::uint64_t& output) const noexcept {
  if (!object.is_valid()) {
    return gneiss::engine::error::invalid_argument;
  }
  const auto found = entities_.find(object.value);
  if (found == entities_.end() || found->second.first != object) {
    return gneiss::engine::error::not_found;
  }
  output = found->second.second;
  return gneiss::engine::error::success;
}

} // namespace gneiss::host
