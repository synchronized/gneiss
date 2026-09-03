// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_scene_inspection.h"

#include <algorithm>
#include <array>
#include <limits>
#include <new>
#include <set>
#include <string_view>
#include <utility>

namespace {

bool equal_transform(const gneiss_transform& left, const gneiss_transform& right) noexcept {
  return std::ranges::equal(left.translation, right.translation) &&
         std::ranges::equal(left.rotation, right.rotation) &&
         std::ranges::equal(left.scale, right.scale);
}

bool equal_node(const gneiss::runtime_internal::runtime_scene_snapshot_node& left,
                const gneiss::runtime_internal::runtime_scene_snapshot_node& right) noexcept {
  return left.id == right.id && left.parent == right.parent && left.uuid == right.uuid &&
         left.prefab_instance_uuid == right.prefab_instance_uuid &&
         left.prefab_source_node_uuid == right.prefab_source_node_uuid && left.name == right.name &&
         equal_transform(left.local_transform, right.local_transform) &&
         left.component_flags == right.component_flags &&
         left.camera.vertical_field_of_view_radians ==
             right.camera.vertical_field_of_view_radians &&
         left.camera.near_plane == right.camera.near_plane &&
         left.camera.far_plane == right.camera.far_plane && left.mesh_uri == right.mesh_uri &&
         left.material_uri == right.material_uri;
}

std::string borrowed_string(const char* value, std::uint64_t length) {
  return length == 0U ? std::string{} : std::string(value, static_cast<std::size_t>(length));
}

std::string author_key(const gneiss::runtime_internal::runtime_scene_source_node& node) {
  if (node.prefab_instance_uuid.empty()) {
    return "node:" + node.uuid;
  }
  return "prefab:" + node.prefab_instance_uuid + ":" + node.prefab_source_node_uuid;
}

} // namespace

namespace gneiss::runtime_internal {

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

result runtime_scene_inspection::capture(std::span<const runtime_scene_source_node> nodes,
                                         bool force_full, runtime_scene_snapshot& output) noexcept {
  if (session_id_ == 0U || nodes.size() > runtime_inspection_max_nodes) {
    return result::invalid_argument;
  }
  try {
    std::set<std::string, std::less<>> author_keys;
    std::set<std::uint64_t> native_nodes;
    for (const auto& node : nodes) {
      if (node.native_node == 0U || node.uuid.empty() ||
          node.uuid.size() > runtime_inspection_max_string_size ||
          node.prefab_instance_uuid.size() > runtime_inspection_max_string_size ||
          node.prefab_source_node_uuid.size() > runtime_inspection_max_string_size ||
          (!node.prefab_source_node_uuid.empty() && node.prefab_instance_uuid.empty()) ||
          node.name.size() > runtime_inspection_max_string_size ||
          node.mesh_uri.size() > runtime_inspection_max_string_size ||
          node.material_uri.size() > runtime_inspection_max_string_size ||
          !author_keys.insert(author_key(node)).second ||
          !native_nodes.insert(node.native_node).second) {
        return result::invalid_argument;
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
          return result::out_of_memory;
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
    std::map<std::uint64_t, std::pair<ipc_runtime_object_id, gneiss_entity_id>> entities;
    std::vector<runtime_scene_snapshot_node> ordered;
    ordered.reserve(nodes.size());
    for (const auto& source : nodes) {
      ipc_runtime_object_id parent;
      if (source.native_parent != 0U) {
        const auto found = native_identities.find(source.native_parent);
        if (found == native_identities.end()) {
          return result::invalid_argument;
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
                                       .component_flags = source.component_flags,
                                       .camera = source.camera,
                                       .mesh_uri = source.mesh_uri,
                                       .material_uri = source.material_uri};
      current.emplace(node.id.value, node);
      if (source.native_entity != GNEISS_NULL_ENTITY_ID) {
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
      return result::out_of_memory;
    }
    if (pending.changes.empty() && !pending.is_full) {
      output = {};
      return result::not_ready;
    }
    if (next_sequence_ == 0U) {
      return result::out_of_memory;
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
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

result runtime_scene_inspection::capture_scene(gneiss_application application,
                                               gneiss_scene_instance scene, bool force_full,
                                               runtime_scene_snapshot& output) noexcept {
  if (application == GNEISS_NULL_APPLICATION || scene == GNEISS_NULL_SCENE_INSTANCE) {
    return result::invalid_argument;
  }
  std::uint64_t count = 0U;
  auto operation = gneiss_scene_instance_get_node_count(application, scene, &count);
  if (operation != GNEISS_SUCCESS) {
    return from_native(operation);
  }
  if (count > runtime_inspection_max_nodes) {
    return result::out_of_memory;
  }
  std::uint64_t prefab_count = 0U;
  operation = gneiss_scene_instance_get_prefab_node_count(application, scene, &prefab_count);
  if (operation != GNEISS_SUCCESS) {
    return from_native(operation);
  }
  if (prefab_count > runtime_inspection_max_nodes - count) {
    return result::out_of_memory;
  }
  gneiss_world world = GNEISS_NULL_WORLD;
  operation = gneiss_application_get_world(application, &world);
  if (operation != GNEISS_SUCCESS) {
    return from_native(operation);
  }
  try {
    std::vector<runtime_scene_source_node> nodes;
    nodes.reserve(static_cast<std::size_t>(count + prefab_count));
    for (std::uint64_t index = 0U; index < count; ++index) {
      gneiss_scene_instance_node_info info = GNEISS_SCENE_INSTANCE_NODE_INFO_INIT;
      operation = gneiss_scene_instance_get_node_info(application, scene, index, &info);
      if (operation != GNEISS_SUCCESS) {
        return from_native(operation);
      }
      if ((info.uuid_length != 0U && info.uuid == nullptr) ||
          (info.name_length != 0U && info.name == nullptr) ||
          (info.mesh_uri_length != 0U && info.mesh_uri == nullptr) ||
          (info.material_uri_length != 0U && info.material_uri == nullptr) ||
          info.uuid_length > runtime_inspection_max_string_size ||
          info.name_length > runtime_inspection_max_string_size ||
          info.mesh_uri_length > runtime_inspection_max_string_size ||
          info.material_uri_length > runtime_inspection_max_string_size) {
        return result::invalid_argument;
      }
      auto runtime_transform = info.local_transform;
      operation = gneiss_world_entity_get_local_transform(world, info.entity, &runtime_transform);
      if (operation != GNEISS_SUCCESS) {
        return from_native(operation);
      }
      nodes.push_back(
          {.native_node = info.node,
           .native_parent = info.parent,
           .native_entity = info.entity,
           .uuid = borrowed_string(info.uuid, info.uuid_length),
           .prefab_instance_uuid = {},
           .prefab_source_node_uuid = {},
           .name = borrowed_string(info.name, info.name_length),
           .local_transform = runtime_transform,
           .component_flags = info.component_flags,
           .camera = info.camera,
           .mesh_uri = borrowed_string(info.mesh_uri, info.mesh_uri_length),
           .material_uri = borrowed_string(info.material_uri, info.material_uri_length)});
    }
    for (std::uint64_t index = 0U; index < prefab_count; ++index) {
      gneiss_scene_prefab_node_info info = GNEISS_SCENE_PREFAB_NODE_INFO_INIT;
      operation = gneiss_scene_instance_get_prefab_node_info(application, scene, index, &info);
      if (operation != GNEISS_SUCCESS) {
        return from_native(operation);
      }
      if (info.instance_uuid == nullptr || info.instance_uuid_length == 0U ||
          (info.source_node_uuid_length != 0U && info.source_node_uuid == nullptr) ||
          (info.name_length != 0U && info.name == nullptr) ||
          info.instance_uuid_length > runtime_inspection_max_string_size ||
          info.source_node_uuid_length > runtime_inspection_max_string_size ||
          info.name_length > runtime_inspection_max_string_size) {
        return result::invalid_argument;
      }
      const auto instance_uuid = borrowed_string(info.instance_uuid, info.instance_uuid_length);
      const auto source_uuid = borrowed_string(info.source_node_uuid, info.source_node_uuid_length);
      auto runtime_transform = info.local_transform;
      operation = gneiss_world_entity_get_local_transform(world, info.entity, &runtime_transform);
      if (operation != GNEISS_SUCCESS) {
        return from_native(operation);
      }
      nodes.push_back({.native_node = info.node,
                       .native_parent = info.parent,
                       .native_entity = info.entity,
                       .uuid = source_uuid.empty() ? instance_uuid : source_uuid,
                       .prefab_instance_uuid = instance_uuid,
                       .prefab_source_node_uuid = source_uuid,
                       .name = borrowed_string(info.name, info.name_length),
                       .local_transform = runtime_transform,
                       .component_flags = 0U,
                       .camera = GNEISS_CAMERA_DESC_INIT,
                       .mesh_uri = {},
                       .material_uri = {}});
    }
    return capture(nodes, force_full, output);
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

result runtime_scene_inspection::resolve_entity(ipc_runtime_object_id object,
                                                gneiss_entity_id& output) const noexcept {
  if (!object.is_valid()) {
    return result::invalid_argument;
  }
  const auto found = entities_.find(object.value);
  if (found == entities_.end() || found->second.first != object) {
    return result::not_found;
  }
  output = found->second.second;
  return result::success;
}

} // namespace gneiss::runtime_internal
