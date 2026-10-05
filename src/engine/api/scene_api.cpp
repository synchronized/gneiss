// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/application/application_registry.hpp"
#include "engine/function/application/application_state.hpp"

#include <gneiss/scene.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <string>
#include <string_view>

namespace {
using gneiss::application_internal::find_application;
using gneiss::application_internal::validate_application;
} // namespace

extern "C" gneiss_result gneiss_scene_instance_load(gneiss_application application, const char* uri,
                                                    uint64_t uri_length,
                                                    gneiss_scene_instance* out_instance) {
  if (out_instance == nullptr || uri == nullptr || uri_length == 0U ||
      uri_length > std::numeric_limits<std::size_t>::max()) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_instance = GNEISS_NULL_SCENE_INSTANCE;
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    if (validation_result != GNEISS_SUCCESS) {
      return validation_result;
    }
    const auto uri_view = std::string_view(uri, static_cast<std::size_t>(uri_length));
    const auto result = state->scenes()->load(uri_view, out_instance);
    if (result != GNEISS_SUCCESS) {
      auto message = std::string{"场景加载失败："};
      message.append(uri_view);
      state->report(application, GNEISS_DIAGNOSTIC_ERROR, GNEISS_DIAGNOSTIC_CATEGORY_ASSET, result,
                    "scene.load", message);
    }
    return result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_scene_instance_create_empty(gneiss_application application,
                                                            const char* scene_uuid,
                                                            uint64_t scene_uuid_length,
                                                            gneiss_scene_instance* out_instance) {
  if (out_instance == nullptr || scene_uuid == nullptr || scene_uuid_length == 0U ||
      scene_uuid_length > std::numeric_limits<std::size_t>::max()) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_instance = GNEISS_NULL_SCENE_INSTANCE;
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS
               ? state->scenes()->create_empty(
                     std::string_view(scene_uuid, static_cast<std::size_t>(scene_uuid_length)),
                     out_instance)
               : validation_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): C ABI 句柄名称区分所属关系。
extern "C" gneiss_result gneiss_scene_instance_unload(gneiss_application application,
                                                      gneiss_scene_instance instance) {
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS ? state->scenes()->unload(instance)
                                               : validation_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): C ABI 句柄名称区分所属关系。
extern "C" gneiss_result gneiss_scene_instance_find_node(gneiss_application application,
                                                         gneiss_scene_instance instance,
                                                         const char* uuid, uint64_t uuid_length,
                                                         gneiss_scene_node_id* out_node) {
  if (out_node == nullptr || uuid == nullptr || uuid_length == 0U ||
      uuid_length > std::numeric_limits<std::size_t>::max()) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_node = GNEISS_NULL_SCENE_NODE_ID;
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    if (validation_result != GNEISS_SUCCESS) {
      return validation_result;
    }
    return state->scenes()->find_node(
        instance, std::string_view(uuid, static_cast<std::size_t>(uuid_length)), out_node);
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_scene_instance_get_node_count(gneiss_application application,
                                                              gneiss_scene_instance instance,
                                                              uint64_t* out_count) {
  if (out_count == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_count = 0U;
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS
               ? state->scenes()->get_node_count(instance, out_count)
               : validation_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result
gneiss_scene_instance_get_node_info(gneiss_application application, gneiss_scene_instance instance,
                                    uint64_t index, gneiss_scene_instance_node_info* out_info) {
  if (out_info == nullptr ||
      out_info->struct_size < GNEISS_SCENE_INSTANCE_NODE_INFO_VERSION_1_SIZE) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS
               ? state->scenes()->get_node_info(instance, index, out_info)
               : validation_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_scene_instance_get_prefab_node_count(gneiss_application application,
                                                                     gneiss_scene_instance instance,
                                                                     uint64_t* out_count) {
  if (out_count == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_count = 0U;
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS
               ? state->scenes()->get_prefab_node_count(instance, out_count)
               : validation_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result
gneiss_scene_instance_get_prefab_node_info(gneiss_application application,
                                           gneiss_scene_instance instance, uint64_t index,
                                           gneiss_scene_prefab_node_info* out_info) {
  if (out_info == nullptr || out_info->struct_size < sizeof(gneiss_scene_prefab_node_info)) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS
               ? state->scenes()->get_prefab_node_info(instance, index, out_info)
               : validation_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_scene_instance_create_prefab_instance(
    gneiss_application application, gneiss_scene_instance instance,
    const gneiss_scene_prefab_instance_desc* desc, gneiss_scene_node_id* out_root) {
  if (out_root == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_root = GNEISS_NULL_SCENE_NODE_ID;
  if (desc == nullptr || desc->struct_size < sizeof(gneiss_scene_prefab_instance_desc) ||
      desc->instance_uuid == nullptr || desc->instance_uuid_length == 0U ||
      (desc->name == nullptr && desc->name_length != 0U) || desc->prefab_uri == nullptr ||
      desc->prefab_uri_length == 0U ||
      desc->instance_uuid_length > std::numeric_limits<std::size_t>::max() ||
      desc->name_length > std::numeric_limits<std::size_t>::max() ||
      desc->prefab_uri_length > std::numeric_limits<std::size_t>::max()) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    if (validation_result != GNEISS_SUCCESS) {
      return validation_result;
    }
    const gneiss::scene_internal::prefab_creation value{
        .instance_uuid =
            {
                desc->instance_uuid,
                static_cast<std::size_t>(desc->instance_uuid_length),
            },
        .name =
            {
                desc->name == nullptr ? "" : desc->name,
                static_cast<std::size_t>(desc->name_length),
            },
        .prefab_uri =
            {
                desc->prefab_uri,
                static_cast<std::size_t>(desc->prefab_uri_length),
            },
        .parent = desc->parent,
        .local_transform = desc->local_transform,
    };
    return state->scenes()->create_prefab_instance(instance, value, out_root);
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_scene_instance_set_prefab_instance_name(
    gneiss_application application, gneiss_scene_instance instance, gneiss_scene_node_id root,
    const char* name, uint64_t name_length) {
  if ((name == nullptr && name_length != 0U) ||
      name_length > std::numeric_limits<std::size_t>::max()) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS
               ? state->scenes()->set_prefab_instance_name(
                     instance, root,
                     std::string_view{name == nullptr ? "" : name,
                                      static_cast<std::size_t>(name_length)})
               : validation_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_scene_instance_set_prefab_source_transform(
    gneiss_application application, gneiss_scene_instance instance, gneiss_scene_node_id node,
    const gneiss_transform* transform) {
  if (transform == nullptr || node == GNEISS_NULL_SCENE_NODE_ID) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS
               ? state->scenes()->set_prefab_source_transform(instance, node, *transform)
               : validation_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_scene_instance_destroy_prefab_instance(
    gneiss_application application, gneiss_scene_instance instance, gneiss_scene_node_id root) {
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS
               ? state->scenes()->destroy_prefab_instance(instance, root)
               : validation_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_scene_instance_refresh_prefab_instance(
    gneiss_application application, gneiss_scene_instance instance, gneiss_scene_node_id root,
    gneiss_scene_node_id* out_new_root, gneiss_scene_prefab_refresh_token* out_token) {
  if (out_new_root == nullptr || out_token == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_new_root = GNEISS_NULL_SCENE_NODE_ID;
  *out_token = GNEISS_NULL_SCENE_PREFAB_REFRESH_TOKEN;
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS
               ? state->scenes()->refresh_prefab_instance(instance, root, out_new_root, out_token)
               : validation_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_scene_instance_toggle_prefab_refresh(
    gneiss_application application, gneiss_scene_instance instance,
    gneiss_scene_prefab_refresh_token token, gneiss_scene_node_id* out_new_root) {
  if (out_new_root == nullptr || token == GNEISS_NULL_SCENE_PREFAB_REFRESH_TOKEN) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_new_root = GNEISS_NULL_SCENE_NODE_ID;
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS
               ? state->scenes()->toggle_prefab_refresh(instance, token, out_new_root)
               : validation_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result
gneiss_scene_instance_release_prefab_refresh(gneiss_application application,
                                             gneiss_scene_instance instance,
                                             gneiss_scene_prefab_refresh_token token) {
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS
               ? state->scenes()->release_prefab_refresh(instance, token)
               : validation_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_scene_instance_create_node(gneiss_application application,
                                                           gneiss_scene_instance instance,
                                                           const gneiss_scene_node_desc* desc,
                                                           gneiss_scene_node_id* out_node) {
  if (desc == nullptr || out_node == nullptr ||
      desc->struct_size < sizeof(gneiss_scene_node_desc) || desc->uuid == nullptr ||
      desc->uuid_length == 0U || (desc->name == nullptr && desc->name_length != 0U) ||
      desc->uuid_length > std::numeric_limits<std::size_t>::max() ||
      desc->name_length > std::numeric_limits<std::size_t>::max()) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_node = GNEISS_NULL_SCENE_NODE_ID;
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    if (validation_result != GNEISS_SUCCESS) {
      return validation_result;
    }
    const gneiss::scene_internal::node_creation value{
        .uuid =
            {
                desc->uuid,
                static_cast<std::size_t>(desc->uuid_length),
            },
        .name =
            {
                desc->name == nullptr ? "" : desc->name,
                static_cast<std::size_t>(desc->name_length),
            },
        .parent = desc->parent,
        .local_transform = desc->local_transform,
    };
    return state->scenes()->create_node(instance, value, out_node);
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_scene_instance_set_node_name(gneiss_application application,
                                                             gneiss_scene_instance instance,
                                                             gneiss_scene_node_id node,
                                                             const char* name,
                                                             uint64_t name_length) {
  if (node == GNEISS_NULL_SCENE_NODE_ID || (name == nullptr && name_length != 0U) ||
      name_length > std::numeric_limits<std::size_t>::max()) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS
               ? state->scenes()->set_node_name(
                     instance, node,
                     std::string_view(name == nullptr ? "" : name,
                                      static_cast<std::size_t>(name_length)))
               : validation_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_scene_instance_reparent_node(gneiss_application application,
                                                             gneiss_scene_instance instance,
                                                             gneiss_scene_node_id node,
                                                             gneiss_scene_node_id parent) {
  if (node == GNEISS_NULL_SCENE_NODE_ID) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS
               ? state->scenes()->reparent_node(instance, node, parent)
               : validation_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_scene_instance_capture_subtree(gneiss_application application,
                                                               gneiss_scene_instance instance,
                                                               gneiss_scene_node_id root,
                                                               char* buffer, uint64_t capacity,
                                                               uint64_t* out_length) {
  if (root == GNEISS_NULL_SCENE_NODE_ID || out_length == nullptr ||
      (buffer == nullptr && capacity != 0U) || capacity > std::numeric_limits<std::size_t>::max()) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_length = 0U;
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    if (validation_result != GNEISS_SUCCESS) {
      return validation_result;
    }
    std::string snapshot;
    const auto result = state->scenes()->capture_subtree(instance, root, snapshot);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    *out_length = snapshot.size();
    if (buffer == nullptr) {
      return capacity == 0U ? GNEISS_SUCCESS : GNEISS_ERROR_INVALID_ARGUMENT;
    }
    if (capacity < snapshot.size()) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    std::ranges::copy(snapshot, buffer);
    return GNEISS_SUCCESS;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result
gneiss_scene_instance_restore_subtree(gneiss_application application,
                                      gneiss_scene_instance instance, const char* snapshot,
                                      uint64_t snapshot_length, gneiss_scene_node_id parent,
                                      const gneiss_scene_uuid_mapping* mappings,
                                      uint64_t mapping_count, gneiss_scene_node_id* out_root) {
  if (snapshot == nullptr || snapshot_length == 0U || out_root == nullptr ||
      snapshot_length > std::numeric_limits<std::size_t>::max() ||
      mapping_count > std::numeric_limits<std::size_t>::max() ||
      (mappings == nullptr && mapping_count != 0U)) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  for (std::uint64_t index = 0; index < mapping_count; ++index) {
    const auto& mapping = mappings[index];
    if (mapping.source_uuid == nullptr || mapping.target_uuid == nullptr ||
        mapping.source_uuid_length == 0U || mapping.target_uuid_length == 0U ||
        mapping.source_uuid_length > std::numeric_limits<std::size_t>::max() ||
        mapping.target_uuid_length > std::numeric_limits<std::size_t>::max()) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
  }
  *out_root = GNEISS_NULL_SCENE_NODE_ID;
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS
               ? state->scenes()->restore_subtree(
                     instance,
                     std::string_view(snapshot, static_cast<std::size_t>(snapshot_length)), parent,
                     mappings, mapping_count, out_root)
               : validation_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_scene_instance_destroy_subtree(gneiss_application application,
                                                               gneiss_scene_instance instance,
                                                               gneiss_scene_node_id root) {
  if (root == GNEISS_NULL_SCENE_NODE_ID) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS ? state->scenes()->destroy_subtree(instance, root)
                                               : validation_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_scene_instance_create_mesh_renderer_node(
    gneiss_application application, gneiss_scene_instance instance,
    const gneiss_scene_mesh_renderer_node_desc* desc, gneiss_scene_node_id* out_node) {
  if (desc == nullptr || out_node == nullptr ||
      desc->struct_size < sizeof(gneiss_scene_mesh_renderer_node_desc) ||
      desc->renderer.struct_size < sizeof(gneiss_scene_mesh_renderer_desc) ||
      desc->uuid == nullptr || desc->uuid_length == 0U ||
      (desc->name == nullptr && desc->name_length != 0U) || desc->renderer.mesh_uri == nullptr ||
      desc->renderer.mesh_uri_length == 0U || desc->renderer.material_uri == nullptr ||
      desc->renderer.material_uri_length == 0U ||
      desc->uuid_length > std::numeric_limits<std::size_t>::max() ||
      desc->name_length > std::numeric_limits<std::size_t>::max() ||
      desc->renderer.mesh_uri_length > std::numeric_limits<std::size_t>::max() ||
      desc->renderer.material_uri_length > std::numeric_limits<std::size_t>::max()) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_node = GNEISS_NULL_SCENE_NODE_ID;
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    if (validation_result != GNEISS_SUCCESS) {
      return validation_result;
    }
    const gneiss::scene_internal::mesh_renderer_node_creation value{
        .uuid =
            {
                desc->uuid,
                static_cast<std::size_t>(desc->uuid_length),
            },
        .name =
            {
                desc->name == nullptr ? "" : desc->name,
                static_cast<std::size_t>(desc->name_length),
            },
        .parent = desc->parent,
        .mesh_uri =
            {
                desc->renderer.mesh_uri,
                static_cast<std::size_t>(desc->renderer.mesh_uri_length),
            },
        .material_uri =
            {
                desc->renderer.material_uri,
                static_cast<std::size_t>(desc->renderer.material_uri_length),
            },
    };
    return state->scenes()->create_mesh_renderer_node(instance, value, out_node);
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result
gneiss_scene_instance_set_mesh_renderer(gneiss_application application,
                                        gneiss_scene_instance instance, gneiss_scene_node_id node,
                                        const gneiss_scene_mesh_renderer_desc* desc) {
  if (desc == nullptr || desc->struct_size < sizeof(gneiss_scene_mesh_renderer_desc) ||
      node == GNEISS_NULL_SCENE_NODE_ID || desc->mesh_uri == nullptr ||
      desc->mesh_uri_length == 0U || desc->material_uri == nullptr ||
      desc->material_uri_length == 0U ||
      desc->mesh_uri_length > std::numeric_limits<std::size_t>::max() ||
      desc->material_uri_length > std::numeric_limits<std::size_t>::max()) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS
               ? state->scenes()->set_mesh_renderer(
                     instance, node,
                     std::string_view(desc->mesh_uri,
                                      static_cast<std::size_t>(desc->mesh_uri_length)),
                     std::string_view(desc->material_uri,
                                      static_cast<std::size_t>(desc->material_uri_length)))
               : validation_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_scene_instance_set_camera(gneiss_application application,
                                                          gneiss_scene_instance instance,
                                                          gneiss_scene_node_id node,
                                                          const gneiss_scene_camera_desc* desc) {
  if (node == GNEISS_NULL_SCENE_NODE_ID || desc == nullptr ||
      desc->struct_size < sizeof(gneiss_scene_camera_desc) ||
      desc->camera.struct_size < sizeof(gneiss_camera_desc)) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS ? state->scenes()->set_camera(instance, node, *desc)
                                               : validation_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_scene_instance_remove_camera(gneiss_application application,
                                                             gneiss_scene_instance instance,
                                                             gneiss_scene_node_id node) {
  if (node == GNEISS_NULL_SCENE_NODE_ID) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS ? state->scenes()->remove_camera(instance, node)
                                               : validation_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_scene_instance_remove_mesh_renderer(gneiss_application application,
                                                                    gneiss_scene_instance instance,
                                                                    gneiss_scene_node_id node) {
  if (node == GNEISS_NULL_SCENE_NODE_ID) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS
               ? state->scenes()->remove_mesh_renderer(instance, node)
               : validation_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_scene_instance_destroy_node(gneiss_application application,
                                                            gneiss_scene_instance instance,
                                                            gneiss_scene_node_id node) {
  if (node == GNEISS_NULL_SCENE_NODE_ID) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS ? state->scenes()->destroy_node(instance, node)
                                               : validation_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): C ABI 句柄名称区分所属关系。
extern "C" gneiss_result gneiss_scene_instance_serialize(gneiss_application application,
                                                         gneiss_scene_instance instance,
                                                         char* buffer, uint64_t capacity,
                                                         uint64_t* out_length) {
  if (out_length == nullptr || (buffer == nullptr && capacity != 0U) ||
      (buffer != nullptr && capacity > std::numeric_limits<std::size_t>::max())) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_length = 0U;
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    if (validation_result != GNEISS_SUCCESS) {
      return validation_result;
    }
    std::string json;
    const auto result = state->scenes()->serialize(instance, json);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    *out_length = json.size();
    if (buffer == nullptr) {
      return capacity == 0U ? GNEISS_SUCCESS : GNEISS_ERROR_INVALID_ARGUMENT;
    }
    if (capacity < json.size()) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    std::ranges::copy(json, buffer);
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}
