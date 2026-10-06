// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/function/scene/scene_query.hpp"

namespace gneiss::api {

/** 调用方已验证最小尺寸；仅写入其声明版本内的字段。 */
inline void write_scene_node(const scene_internal::scene_node_view& value,
                             gneiss_scene_instance_node_info& output) noexcept {
  output.reserved = 0U;
  output.node = value.node;
  output.parent = value.parent;
  output.entity = value.entity;
  output.uuid = value.uuid.empty() ? nullptr : value.uuid.data();
  output.uuid_length = value.uuid.size();
  output.name = value.name.empty() ? nullptr : value.name.data();
  output.name_length = value.name.size();
  output.reserved_2[0] = 0U;
  output.reserved_2[1] = 0U;
  if (output.struct_size >= GNEISS_SCENE_INSTANCE_NODE_INFO_VERSION_2_SIZE) {
    output.mesh_uri = value.mesh_uri.empty() ? nullptr : value.mesh_uri.data();
    output.mesh_uri_length = value.mesh_uri.size();
    output.material_uri = value.material_uri.empty() ? nullptr : value.material_uri.data();
    output.material_uri_length = value.material_uri.size();
  }
  if (output.struct_size >= GNEISS_SCENE_INSTANCE_NODE_INFO_VERSION_3_SIZE) {
    output.local_transform = value.local_transform;
    output.component_flags = value.component_flags;
    output.reserved_3 = 0U;
    output.camera = GNEISS_CAMERA_DESC_INIT;
    if (value.camera) {
      output.camera.vertical_field_of_view_radians = value.camera->vertical_field_of_view_radians;
      output.camera.near_plane = value.camera->near_plane;
      output.camera.far_plane = value.camera->far_plane;
    }
  }
}

inline void write_prefab_node(const scene_internal::prefab_node_view& value,
                              gneiss_scene_prefab_node_info& output) noexcept {
  const auto size = output.struct_size;
  output = GNEISS_SCENE_PREFAB_NODE_INFO_INIT;
  output.struct_size = size;
  output.flags = value.flags;
  output.node = value.node;
  output.parent = value.parent;
  output.entity = value.entity;
  output.instance_uuid = value.instance_uuid.empty() ? nullptr : value.instance_uuid.data();
  output.instance_uuid_length = value.instance_uuid.size();
  output.source_node_uuid =
      value.source_node_uuid.empty() ? nullptr : value.source_node_uuid.data();
  output.source_node_uuid_length = value.source_node_uuid.size();
  output.name = value.name.empty() ? nullptr : value.name.data();
  output.name_length = value.name.size();
  output.prefab_uri = value.prefab_uri.empty() ? nullptr : value.prefab_uri.data();
  output.prefab_uri_length = value.prefab_uri.size();
  output.local_transform = value.local_transform;
  output.source_local_transform = value.source_local_transform;
}

} // namespace gneiss::api
