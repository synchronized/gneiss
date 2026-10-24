// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/api/result_conversion.hpp"
#include "engine/api/scene_query_conversion.hpp"
#include "engine/core/identifier_conversion.hpp"
#include "engine/core/result.hpp"
#include "engine/function/scene/scene_instances.hpp"
#include "runtime/application.hpp"
#include "runtime/application_registry.hpp"

#include <gneiss/engine/scene.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <string>
#include <string_view>
#include <vector>

namespace {
using gneiss::runtime::find_application;
using gneiss::runtime::validate_application;
} // namespace

extern "C" gneiss_result gneiss_scene_instance_load(gneiss_application application, const char* uri,
                                                    uint64_t uri_length,
                                                    gneiss_scene_instance* out_instance) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    if (out_instance == nullptr || uri == nullptr || uri_length == 0U ||
        uri_length > std::numeric_limits<std::size_t>::max()) {
      return gneiss::engine::result::invalid_argument;
    }
    *out_instance = GNEISS_NULL_SCENE_INSTANCE;
    try {
      auto state = find_application(application);
      const auto validation_result = validate_application(state);
      if (validation_result != gneiss::engine::result::success) {
        return validation_result;
      }
      const auto uri_view = std::string_view(uri, static_cast<std::size_t>(uri_length));
      const auto result = state->scenes()->load(uri_view, out_instance);
      if (result != gneiss::engine::result::success) {
        auto message = std::string{"场景加载失败："};
        message.append(uri_view);
        state->report(application, gneiss::engine::severity::error, gneiss::engine::category::asset,
                      result, "scene.load", message);
      }
      return result;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}

extern "C" gneiss_result gneiss_scene_instance_create_empty(gneiss_application application,
                                                            const char* scene_uuid,
                                                            uint64_t scene_uuid_length,
                                                            gneiss_scene_instance* out_instance) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    if (out_instance == nullptr || scene_uuid == nullptr || scene_uuid_length == 0U ||
        scene_uuid_length > std::numeric_limits<std::size_t>::max()) {
      return gneiss::engine::result::invalid_argument;
    }
    *out_instance = GNEISS_NULL_SCENE_INSTANCE;
    try {
      auto state = find_application(application);
      const auto validation_result = validate_application(state);
      return validation_result == gneiss::engine::result::success
                 ? state->scenes()->create_empty(
                       std::string_view(scene_uuid, static_cast<std::size_t>(scene_uuid_length)),
                       out_instance)
                 : validation_result;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): C ABI 句柄名称区分所属关系。
extern "C" gneiss_result gneiss_scene_instance_unload(gneiss_application application,
                                                      gneiss_scene_instance instance) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    try {
      auto state = find_application(application);
      const auto validation_result = validate_application(state);
      return validation_result == gneiss::engine::result::success
                 ? state->scenes()->unload(instance)
                 : validation_result;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): C ABI 句柄名称区分所属关系。
extern "C" gneiss_result gneiss_scene_instance_find_node(gneiss_application application,
                                                         gneiss_scene_instance instance,
                                                         const char* uuid, uint64_t uuid_length,
                                                         gneiss_scene_node_id* out_node) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    if (out_node == nullptr || uuid == nullptr || uuid_length == 0U ||
        uuid_length > std::numeric_limits<std::size_t>::max()) {
      return gneiss::engine::result::invalid_argument;
    }
    *out_node = GNEISS_NULL_SCENE_NODE_ID;
    try {
      auto state = find_application(application);
      const auto validation_result = validate_application(state);
      if (validation_result != gneiss::engine::result::success) {
        return validation_result;
      }
      return gneiss::engine::write_identifier<gneiss::scene_node_id>(
          *out_node, [&](auto& native_output) {
            return state->scenes()->find_node(
                instance, std::string_view(uuid, static_cast<std::size_t>(uuid_length)),
                native_output);
          });
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}

extern "C" gneiss_result gneiss_scene_instance_get_node_count(gneiss_application application,
                                                              gneiss_scene_instance instance,
                                                              uint64_t* out_count) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    if (out_count == nullptr) {
      return gneiss::engine::result::invalid_argument;
    }
    *out_count = 0U;
    try {
      auto state = find_application(application);
      const auto validation_result = validate_application(state);
      return validation_result == gneiss::engine::result::success
                 ? state->scenes()->get_node_count(instance, out_count)
                 : validation_result;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}

extern "C" gneiss_result
gneiss_scene_instance_get_node_info(gneiss_application application, gneiss_scene_instance instance,
                                    uint64_t index, gneiss_scene_instance_node_info* out_info) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    if (out_info == nullptr ||
        out_info->struct_size < GNEISS_SCENE_INSTANCE_NODE_INFO_VERSION_1_SIZE) {
      return gneiss::engine::result::invalid_argument;
    }
    try {
      auto state = find_application(application);
      const auto validation_result = validate_application(state);
      if (validation_result != gneiss::engine::result::success) {
        return validation_result;
      }
      using gneiss::engine::node_query_detail;
      auto detail = node_query_detail::identity;
      if (out_info->struct_size >= GNEISS_SCENE_INSTANCE_NODE_INFO_VERSION_3_SIZE) {
        detail = node_query_detail::components;
      } else if (out_info->struct_size >= GNEISS_SCENE_INSTANCE_NODE_INFO_VERSION_2_SIZE) {
        detail = node_query_detail::assets;
      }
      gneiss::engine::scene_node_view value;
      const auto result = state->scenes()->get_node_info(instance, index, value, detail);
      gneiss::engine::detail::write_scene_node(value, *out_info);
      return result;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}

extern "C" gneiss_result gneiss_scene_instance_get_prefab_node_count(gneiss_application application,
                                                                     gneiss_scene_instance instance,
                                                                     uint64_t* out_count) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    if (out_count == nullptr) {
      return gneiss::engine::result::invalid_argument;
    }
    *out_count = 0U;
    try {
      auto state = find_application(application);
      const auto validation_result = validate_application(state);
      return validation_result == gneiss::engine::result::success
                 ? state->scenes()->get_prefab_node_count(instance, out_count)
                 : validation_result;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}

extern "C" gneiss_result
gneiss_scene_instance_get_prefab_node_info(gneiss_application application,
                                           gneiss_scene_instance instance, uint64_t index,
                                           gneiss_scene_prefab_node_info* out_info) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    if (out_info == nullptr || out_info->struct_size < sizeof(gneiss_scene_prefab_node_info)) {
      return gneiss::engine::result::invalid_argument;
    }
    try {
      auto state = find_application(application);
      const auto validation_result = validate_application(state);
      if (validation_result != gneiss::engine::result::success) {
        return validation_result;
      }
      gneiss::engine::prefab_node_view value;
      const auto result = state->scenes()->get_prefab_node_info(instance, index, value);
      gneiss::engine::detail::write_prefab_node(value, *out_info);
      return result;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}

extern "C" gneiss_result gneiss_scene_instance_refresh_prefab_instance(
    gneiss_application application, gneiss_scene_instance instance, gneiss_scene_node_id root,
    gneiss_scene_node_id* out_new_root, gneiss_scene_prefab_refresh_token* out_token) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    if (out_new_root == nullptr || out_token == nullptr) {
      return gneiss::engine::result::invalid_argument;
    }
    *out_new_root = GNEISS_NULL_SCENE_NODE_ID;
    *out_token = GNEISS_NULL_SCENE_PREFAB_REFRESH_TOKEN;
    try {
      auto state = find_application(application);
      const auto validation_result = validate_application(state);
      return validation_result == gneiss::engine::result::success
                 ? gneiss::engine::write_identifier<gneiss::scene_node_id>(
                       *out_new_root,
                       [&](auto& native_output) {
                         return state->scenes()->refresh_prefab_instance(
                             instance, gneiss::scene_node_id{root}, native_output, out_token);
                       })
                 : validation_result;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}

extern "C" gneiss_result gneiss_scene_instance_toggle_prefab_refresh(
    gneiss_application application, gneiss_scene_instance instance,
    gneiss_scene_prefab_refresh_token token, gneiss_scene_node_id* out_new_root) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    if (out_new_root == nullptr || token == GNEISS_NULL_SCENE_PREFAB_REFRESH_TOKEN) {
      return gneiss::engine::result::invalid_argument;
    }
    *out_new_root = GNEISS_NULL_SCENE_NODE_ID;
    try {
      auto state = find_application(application);
      const auto validation_result = validate_application(state);
      return validation_result == gneiss::engine::result::success
                 ? gneiss::engine::write_identifier<gneiss::scene_node_id>(
                       *out_new_root,
                       [&](auto& native_output) {
                         return state->scenes()->toggle_prefab_refresh(instance, token,
                                                                       native_output);
                       })
                 : validation_result;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}

extern "C" gneiss_result
gneiss_scene_instance_release_prefab_refresh(gneiss_application application,
                                             gneiss_scene_instance instance,
                                             gneiss_scene_prefab_refresh_token token) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    try {
      auto state = find_application(application);
      const auto validation_result = validate_application(state);
      return validation_result == gneiss::engine::result::success
                 ? state->scenes()->release_prefab_refresh(instance, token)
                 : validation_result;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}

extern "C" gneiss_result gneiss_scene_instance_create_node(gneiss_application application,
                                                           gneiss_scene_instance instance,
                                                           const gneiss_scene_node_desc* desc,
                                                           gneiss_scene_node_id* out_node) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    if (desc == nullptr || out_node == nullptr ||
        desc->struct_size < sizeof(gneiss_scene_node_desc) || desc->uuid == nullptr ||
        desc->uuid_length == 0U || (desc->name == nullptr && desc->name_length != 0U) ||
        desc->uuid_length > std::numeric_limits<std::size_t>::max() ||
        desc->name_length > std::numeric_limits<std::size_t>::max()) {
      return gneiss::engine::result::invalid_argument;
    }
    *out_node = GNEISS_NULL_SCENE_NODE_ID;
    try {
      auto state = find_application(application);
      const auto validation_result = validate_application(state);
      if (validation_result != gneiss::engine::result::success) {
        return validation_result;
      }
      const gneiss::engine::node_creation value{
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
          .parent = gneiss::scene_node_id{desc->parent},
          .local_transform = gneiss::engine::detail::read_transform(desc->local_transform),
      };
      return gneiss::engine::write_identifier<gneiss::scene_node_id>(
          *out_node, [&](auto& native_output) {
            return state->scenes()->create_node(instance, value, native_output);
          });
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}

extern "C" gneiss_result gneiss_scene_instance_set_node_name(gneiss_application application,
                                                             gneiss_scene_instance instance,
                                                             gneiss_scene_node_id node,
                                                             const char* name,
                                                             uint64_t name_length) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    if (node == GNEISS_NULL_SCENE_NODE_ID || (name == nullptr && name_length != 0U) ||
        name_length > std::numeric_limits<std::size_t>::max()) {
      return gneiss::engine::result::invalid_argument;
    }
    try {
      auto state = find_application(application);
      const auto validation_result = validate_application(state);
      return validation_result == gneiss::engine::result::success
                 ? state->scenes()->set_node_name(
                       instance, gneiss::scene_node_id{node},
                       std::string_view(name == nullptr ? "" : name,
                                        static_cast<std::size_t>(name_length)))
                 : validation_result;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}

extern "C" gneiss_result gneiss_scene_instance_reparent_node(gneiss_application application,
                                                             gneiss_scene_instance instance,
                                                             gneiss_scene_node_id node,
                                                             gneiss_scene_node_id parent) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    if (node == GNEISS_NULL_SCENE_NODE_ID) {
      return gneiss::engine::result::invalid_argument;
    }
    try {
      auto state = find_application(application);
      const auto validation_result = validate_application(state);
      return validation_result == gneiss::engine::result::success
                 ? state->scenes()->reparent_node(instance, gneiss::scene_node_id{node},
                                                  gneiss::scene_node_id{parent})
                 : validation_result;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}

extern "C" gneiss_result gneiss_scene_instance_capture_subtree(gneiss_application application,
                                                               gneiss_scene_instance instance,
                                                               gneiss_scene_node_id root,
                                                               char* buffer, uint64_t capacity,
                                                               uint64_t* out_length) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    if (root == GNEISS_NULL_SCENE_NODE_ID || out_length == nullptr ||
        (buffer == nullptr && capacity != 0U) ||
        capacity > std::numeric_limits<std::size_t>::max()) {
      return gneiss::engine::result::invalid_argument;
    }
    *out_length = 0U;
    try {
      auto state = find_application(application);
      const auto validation_result = validate_application(state);
      if (validation_result != gneiss::engine::result::success) {
        return validation_result;
      }
      std::string snapshot;
      const auto result =
          state->scenes()->capture_subtree(instance, gneiss::scene_node_id{root}, snapshot);
      if (result != gneiss::engine::result::success) {
        return result;
      }
      *out_length = snapshot.size();
      if (buffer == nullptr) {
        return capacity == 0U ? gneiss::engine::result::success
                              : gneiss::engine::result::invalid_argument;
      }
      if (capacity < snapshot.size()) {
        return gneiss::engine::result::invalid_argument;
      }
      std::ranges::copy(snapshot, buffer);
      return gneiss::engine::result::success;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}

extern "C" gneiss_result
gneiss_scene_instance_restore_subtree(gneiss_application application,
                                      gneiss_scene_instance instance, const char* snapshot,
                                      uint64_t snapshot_length, gneiss_scene_node_id parent,
                                      const gneiss_scene_uuid_mapping* mappings,
                                      uint64_t mapping_count, gneiss_scene_node_id* out_root) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    if (snapshot == nullptr || snapshot_length == 0U || out_root == nullptr ||
        snapshot_length > std::numeric_limits<std::size_t>::max() ||
        mapping_count > std::numeric_limits<std::size_t>::max() ||
        (mappings == nullptr && mapping_count != 0U)) {
      return gneiss::engine::result::invalid_argument;
    }
    for (std::uint64_t index = 0; index < mapping_count; ++index) {
      const auto& mapping = mappings[index];
      if (mapping.source_uuid == nullptr || mapping.target_uuid == nullptr ||
          mapping.source_uuid_length == 0U || mapping.target_uuid_length == 0U ||
          mapping.source_uuid_length > std::numeric_limits<std::size_t>::max() ||
          mapping.target_uuid_length > std::numeric_limits<std::size_t>::max()) {
        return gneiss::engine::result::invalid_argument;
      }
    }
    *out_root = GNEISS_NULL_SCENE_NODE_ID;
    try {
      auto state = find_application(application);
      const auto validation_result = validate_application(state);
      if (validation_result != gneiss::engine::result::success) {
        return validation_result;
      }
      std::vector<gneiss::engine::uuid_mapping> values;
      values.reserve(static_cast<std::size_t>(mapping_count));
      for (std::uint64_t index = 0; index < mapping_count; ++index) {
        const auto& mapping = mappings[index];
        values.push_back({
            .source_uuid = std::string_view(mapping.source_uuid,
                                            static_cast<std::size_t>(mapping.source_uuid_length)),
            .target_uuid = std::string_view(mapping.target_uuid,
                                            static_cast<std::size_t>(mapping.target_uuid_length)),
        });
      }
      return gneiss::engine::write_identifier<gneiss::scene_node_id>(
          *out_root, [&](auto& native_output) {
            return state->scenes()->restore_subtree(
                instance, std::string_view(snapshot, static_cast<std::size_t>(snapshot_length)),
                gneiss::scene_node_id{parent}, values, native_output);
          });
    } catch (const std::bad_alloc&) {
      return gneiss::engine::result::out_of_memory;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}

extern "C" gneiss_result gneiss_scene_instance_destroy_subtree(gneiss_application application,
                                                               gneiss_scene_instance instance,
                                                               gneiss_scene_node_id root) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    if (root == GNEISS_NULL_SCENE_NODE_ID) {
      return gneiss::engine::result::invalid_argument;
    }
    try {
      auto state = find_application(application);
      const auto validation_result = validate_application(state);
      return validation_result == gneiss::engine::result::success
                 ? state->scenes()->destroy_subtree(instance, gneiss::scene_node_id{root})
                 : validation_result;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}

extern "C" gneiss_result gneiss_scene_instance_create_mesh_renderer_node(
    gneiss_application application, gneiss_scene_instance instance,
    const gneiss_scene_mesh_renderer_node_desc* desc, gneiss_scene_node_id* out_node) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
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
      return gneiss::engine::result::invalid_argument;
    }
    *out_node = GNEISS_NULL_SCENE_NODE_ID;
    try {
      auto state = find_application(application);
      const auto validation_result = validate_application(state);
      if (validation_result != gneiss::engine::result::success) {
        return validation_result;
      }
      const gneiss::engine::mesh_renderer_node_creation value{
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
          .parent = gneiss::scene_node_id{desc->parent},
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
      return gneiss::engine::write_identifier<gneiss::scene_node_id>(
          *out_node, [&](auto& native_output) {
            return state->scenes()->create_mesh_renderer_node(instance, value, native_output);
          });
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}

extern "C" gneiss_result
gneiss_scene_instance_set_mesh_renderer(gneiss_application application,
                                        gneiss_scene_instance instance, gneiss_scene_node_id node,
                                        const gneiss_scene_mesh_renderer_desc* desc) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    if (desc == nullptr || desc->struct_size < sizeof(gneiss_scene_mesh_renderer_desc) ||
        node == GNEISS_NULL_SCENE_NODE_ID || desc->mesh_uri == nullptr ||
        desc->mesh_uri_length == 0U || desc->material_uri == nullptr ||
        desc->material_uri_length == 0U ||
        desc->mesh_uri_length > std::numeric_limits<std::size_t>::max() ||
        desc->material_uri_length > std::numeric_limits<std::size_t>::max()) {
      return gneiss::engine::result::invalid_argument;
    }
    try {
      auto state = find_application(application);
      const auto validation_result = validate_application(state);
      return validation_result == gneiss::engine::result::success
                 ? state->scenes()->set_mesh_renderer(
                       instance, gneiss::scene_node_id{node},
                       std::string_view(desc->mesh_uri,
                                        static_cast<std::size_t>(desc->mesh_uri_length)),
                       std::string_view(desc->material_uri,
                                        static_cast<std::size_t>(desc->material_uri_length)))
                 : validation_result;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}

extern "C" gneiss_result gneiss_scene_instance_set_camera(gneiss_application application,
                                                          gneiss_scene_instance instance,
                                                          gneiss_scene_node_id node,
                                                          const gneiss_scene_camera_desc* desc) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    if (node == GNEISS_NULL_SCENE_NODE_ID || desc == nullptr ||
        desc->struct_size < sizeof(gneiss_scene_camera_desc) ||
        desc->camera.struct_size < sizeof(gneiss_camera_desc)) {
      return gneiss::engine::result::invalid_argument;
    }
    try {
      auto state = find_application(application);
      const auto validation_result = validate_application(state);
      if (validation_result != gneiss::engine::result::success) {
        return validation_result;
      }
      if (desc->camera.reserved != 0U) {
        // 保留既有错误顺序：场景/作者节点无效时优先报告句柄错误。
        const auto node_result =
            state->scenes()->validate_editable_node(instance, gneiss::scene_node_id{node});
        return node_result == gneiss::engine::result::success
                   ? gneiss::engine::result::invalid_argument
                   : node_result;
      }
      return state->scenes()->set_camera(
          instance, gneiss::scene_node_id{node},
          {
              .vertical_field_of_view_radians = desc->camera.vertical_field_of_view_radians,
              .near_plane = desc->camera.near_plane,
              .far_plane = desc->camera.far_plane,
              .is_primary = desc->is_primary != 0U,
          });
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}

extern "C" gneiss_result gneiss_scene_instance_remove_camera(gneiss_application application,
                                                             gneiss_scene_instance instance,
                                                             gneiss_scene_node_id node) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    if (node == GNEISS_NULL_SCENE_NODE_ID) {
      return gneiss::engine::result::invalid_argument;
    }
    try {
      auto state = find_application(application);
      const auto validation_result = validate_application(state);
      return validation_result == gneiss::engine::result::success
                 ? state->scenes()->remove_camera(instance, gneiss::scene_node_id{node})
                 : validation_result;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}

extern "C" gneiss_result gneiss_scene_instance_remove_mesh_renderer(gneiss_application application,
                                                                    gneiss_scene_instance instance,
                                                                    gneiss_scene_node_id node) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    if (node == GNEISS_NULL_SCENE_NODE_ID) {
      return gneiss::engine::result::invalid_argument;
    }
    try {
      auto state = find_application(application);
      const auto validation_result = validate_application(state);
      return validation_result == gneiss::engine::result::success
                 ? state->scenes()->remove_mesh_renderer(instance, gneiss::scene_node_id{node})
                 : validation_result;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}

extern "C" gneiss_result gneiss_scene_instance_destroy_node(gneiss_application application,
                                                            gneiss_scene_instance instance,
                                                            gneiss_scene_node_id node) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    if (node == GNEISS_NULL_SCENE_NODE_ID) {
      return gneiss::engine::result::invalid_argument;
    }
    try {
      auto state = find_application(application);
      const auto validation_result = validate_application(state);
      return validation_result == gneiss::engine::result::success
                 ? state->scenes()->destroy_node(instance, gneiss::scene_node_id{node})
                 : validation_result;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): C ABI 句柄名称区分所属关系。
extern "C" gneiss_result gneiss_scene_instance_serialize(gneiss_application application,
                                                         gneiss_scene_instance instance,
                                                         char* buffer, uint64_t capacity,
                                                         uint64_t* out_length) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    if (out_length == nullptr || (buffer == nullptr && capacity != 0U) ||
        (buffer != nullptr && capacity > std::numeric_limits<std::size_t>::max())) {
      return gneiss::engine::result::invalid_argument;
    }
    *out_length = 0U;
    try {
      auto state = find_application(application);
      const auto validation_result = validate_application(state);
      if (validation_result != gneiss::engine::result::success) {
        return validation_result;
      }
      std::string json;
      const auto result = state->scenes()->serialize(instance, json);
      if (result != gneiss::engine::result::success) {
        return result;
      }
      *out_length = json.size();
      if (buffer == nullptr) {
        return capacity == 0U ? gneiss::engine::result::success
                              : gneiss::engine::result::invalid_argument;
      }
      if (capacity < json.size()) {
        return gneiss::engine::result::invalid_argument;
      }
      std::ranges::copy(json, buffer);
      return gneiss::engine::result::success;
    } catch (const std::bad_alloc&) {
      return gneiss::engine::result::out_of_memory;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}
