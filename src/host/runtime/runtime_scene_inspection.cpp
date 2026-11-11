// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_scene_inspection.hpp"
#include "application/application.hpp"
#include "engine/core/error.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/scene/component_registration.hpp"
#include "engine/function/scene/scene_types.hpp"
#include "engine/function/scene/world.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <new>
#include <set>
#include <string_view>
#include <utility>

namespace {

bool equal_transform(const gneiss::math::transform& left,
                     const gneiss::math::transform& right) noexcept {
  return (left.translation == right.translation) && (left.rotation == right.rotation) &&
         (left.scale == right.scale);
}

bool equal_node(const gneiss::host::runtime_scene_snapshot_node& left,
                const gneiss::host::runtime_scene_snapshot_node& right) {
  return left.id == right.id && left.parent == right.parent && left.uuid == right.uuid &&
         left.prefab_instance_uuid == right.prefab_instance_uuid &&
         left.prefab_source_node_uuid == right.prefab_source_node_uuid && left.name == right.name &&
         equal_transform(left.local_transform, right.local_transform) &&
         gneiss::equal_components(left.components, right.components);
}

std::pair<std::array<gneiss::guid, 3>, std::uint64_t>
author_key(const gneiss::host::runtime_scene_source_node& node) {
  if (node.prefab_instance_uuid.is_nil()) {
    return {{node.uuid, {}, {}}, node.uuid ? 0U : node.native_node};
  }
  return {{gneiss::guid{}, node.prefab_instance_uuid, node.prefab_source_node_uuid}, 0U};
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
  sources_.clear();
  is_initialized_ = false;
}

gneiss::error runtime_scene_inspection::set_source_bindings(
    std::span<const gneiss::scene_object_binding> bindings) {
  std::map<std::uint64_t, gneiss::scene_object_binding> next;
  std::set<std::pair<gneiss::guid, gneiss::guid>> addresses;
  for (const auto& binding : bindings) {
    if (!binding.object || !binding.entity ||
        !addresses.emplace(binding.object, binding.source).second ||
        !next.emplace(binding.entity.get(), binding).second)
      return gneiss::error::invalid_argument;
  }
  sources_.swap(next);
  return gneiss::error::success;
}

gneiss::error
runtime_scene_inspection::capture(std::span<const runtime_scene_source_node> nodes, bool force_full,
                                  runtime_scene_snapshot& output) {
  if (session_id_ == 0U || nodes.size() > runtime_inspection_max_nodes) {
    return gneiss::error::invalid_argument;
  }
  {
    std::set<decltype(author_key(runtime_scene_source_node{}))> author_keys;
    std::set<std::uint64_t> native_nodes;
    for (const auto& node : nodes) {
      if (node.native_node == 0U ||
          (!node.prefab_source_node_uuid.is_nil() && node.prefab_instance_uuid.is_nil()) ||
          node.name.size() > runtime_inspection_max_string_size ||
          !author_keys.insert(author_key(node)).second ||
          !native_nodes.insert(node.native_node).second) {
        return gneiss::error::invalid_argument;
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
          return gneiss::error::out_of_memory;
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
          return gneiss::error::invalid_argument;
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
      return gneiss::error::out_of_memory;
    }
    if (pending.changes.empty() && !pending.is_full) {
      output = {};
      return gneiss::error::not_ready;
    }
    if (next_sequence_ == 0U) {
      return gneiss::error::out_of_memory;
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
    return gneiss::error::success;
  }
}

gneiss::error
runtime_scene_inspection::capture_scene(::gneiss::runtime::application& application,
                                        gneiss::scene_instance_id scene, bool force_full,
                                        runtime_scene_snapshot& output) {
  if (!application.is_owner_thread() || !application.accepts_requests())
    return gneiss::error::invalid_state;
  if (!scene.is_valid()) {
    return gneiss::error::invalid_argument;
  }
  auto instance = gneiss::scene_instance::borrow(*application.native_world(), scene);
  if (!instance)
    return instance.error();
  auto count = instance->size();
  if (!count)
    return count.error();
  if (*count > runtime_inspection_max_nodes)
    return gneiss::error::out_of_memory;
  std::vector<runtime_scene_source_node> nodes;
  nodes.reserve(static_cast<std::size_t>(*count));
  auto status = instance->visit_objects([&](gneiss::game_object object) -> gneiss::error {
    const std::string name(object.name());
    if (name.size() > runtime_inspection_max_string_size)
      return gneiss::error::invalid_argument;
    auto parent = object.parent();
    if (!parent)
      return parent.error();
    auto transform = object.local_transform();
    if (!transform)
      return transform.error();
    auto components = gneiss::capture_components(*object.owner(), entity_id{object.entity().get()});
    if (!components)
      return components.error();
    auto source = sources_.find(object.entity().get());
    const auto* binding = source == sources_.end() ? nullptr : &source->second;
    nodes.push_back(
        {.native_node = object.entity().get(),
         .native_parent = parent->entity().get(),
         .native_entity = object.entity().get(),
         .uuid = binding && !binding->source ? binding->object : gneiss::guid{},
         .prefab_instance_uuid = binding && binding->source ? binding->object : gneiss::guid{},
         .prefab_source_node_uuid = binding ? binding->source : gneiss::guid{},
         .name = name,
         .local_transform = *transform,
         .components = std::move(*components)});
    return gneiss::error::success;
  });
  if (status.failed())
    return status;
  // IPC 的展示顺序由宿主决定，不依赖 ECS 遍历顺序。
  std::ranges::sort(nodes, {}, &runtime_scene_source_node::native_node);
  return capture(nodes, force_full, output);
}

gneiss::error
runtime_scene_inspection::resolve_entity(ipc_runtime_object_id object,
                                         std::uint64_t& output) const noexcept {
  if (!object.is_valid()) {
    return gneiss::error::invalid_argument;
  }
  const auto found = entities_.find(object.value);
  if (found == entities_.end() || found->second.first != object) {
    return gneiss::error::not_found;
  }
  output = found->second.second;
  return gneiss::error::success;
}

} // namespace gneiss::host
