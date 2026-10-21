// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_DETAIL_SCENE_INL_
#define GNEISS_DETAIL_SCENE_INL_

#include <gneiss/engine/scene.hpp>

#include <limits>
#include <new>
#include <stdexcept>
#include <vector>

namespace gneiss {

namespace detail {
inline std::string_view scene_text(const char* text, std::uint64_t length) noexcept {
  return text == nullptr ? std::string_view{}
                         : std::string_view{text, static_cast<std::size_t>(length)};
}
} // namespace detail

[[nodiscard]] constexpr gneiss_transform to_native(const transform& value) noexcept {
  return {
      .translation = {value.translation[0], value.translation[1], value.translation[2]},
      .rotation = {value.rotation[0], value.rotation[1], value.rotation[2], value.rotation[3]},
      .scale = {value.scale[0], value.scale[1], value.scale[2]},
  };
}

[[nodiscard]] constexpr transform from_native(const gneiss_transform& value) noexcept {
  return {
      .translation = {value.translation[0], value.translation[1], value.translation[2]},
      .rotation = {value.rotation[0], value.rotation[1], value.rotation[2], value.rotation[3]},
      .scale = {value.scale[0], value.scale[1], value.scale[2]},
  };
}

[[nodiscard]] inline gneiss_scene_node_desc to_native(const scene_node_desc& value) noexcept {
  gneiss_scene_node_desc native{};
  native.struct_size = sizeof(native);
  native.parent = value.parent.get();
  native.uuid = value.uuid.data();
  native.uuid_length = value.uuid.size();
  native.name = value.name.data();
  native.name_length = value.name.size();
  native.local_transform = to_native(value.local_transform);
  return native;
}

[[nodiscard]] inline gneiss_scene_prefab_instance_desc
to_native(const scene_prefab_instance_desc& value) noexcept {
  gneiss_scene_prefab_instance_desc native{};
  native.struct_size = sizeof(native);
  native.parent = value.parent.get();
  native.instance_uuid = value.instance_uuid.data();
  native.instance_uuid_length = value.instance_uuid.size();
  native.name = value.name.data();
  native.name_length = value.name.size();
  native.prefab_uri = value.prefab_uri.data();
  native.prefab_uri_length = value.prefab_uri.size();
  native.local_transform = to_native(value.local_transform);
  return native;
}

[[nodiscard]] inline gneiss_scene_uuid_mapping to_native(const scene_uuid_mapping& value) noexcept {
  gneiss_scene_uuid_mapping native{};
  native.source_uuid = value.source_uuid.data();
  native.source_uuid_length = value.source_uuid.size();
  native.target_uuid = value.target_uuid.data();
  native.target_uuid_length = value.target_uuid.size();
  return native;
}

[[nodiscard]] inline gneiss_scene_mesh_renderer_desc
to_native(const scene_mesh_renderer_desc& value) noexcept {
  gneiss_scene_mesh_renderer_desc native{};
  native.struct_size = sizeof(native);
  native.mesh_uri = value.mesh_uri.data();
  native.mesh_uri_length = value.mesh_uri.size();
  native.material_uri = value.material_uri.data();
  native.material_uri_length = value.material_uri.size();
  return native;
}

[[nodiscard]] inline gneiss_scene_camera_desc to_native(const scene_camera_desc& value) noexcept {
  gneiss_scene_camera_desc native{};
  native.struct_size = sizeof(native);
  native.camera = to_native(value.camera);
  native.is_primary = static_cast<std::uint8_t>(value.is_primary);
  return native;
}

[[nodiscard]] inline gneiss_scene_mesh_renderer_node_desc
to_native(const scene_mesh_renderer_node_desc& value) noexcept {
  gneiss_scene_mesh_renderer_node_desc native{};
  native.struct_size = sizeof(native);
  native.parent = value.parent.get();
  native.uuid = value.uuid.data();
  native.uuid_length = value.uuid.size();
  native.name = value.name.data();
  native.name_length = value.name.size();
  native.renderer = to_native(value.renderer);
  return native;
}

[[nodiscard]] inline scene_instance_node_info
from_native(const gneiss_scene_instance_node_info& value) noexcept {
  return {
      .node = scene_node_id{value.node},
      .parent = scene_node_id{value.parent},
      .entity = entity_id{value.entity},
      .uuid = detail::scene_text(value.uuid, value.uuid_length),
      .name = detail::scene_text(value.name, value.name_length),
      .mesh_uri = detail::scene_text(value.mesh_uri, value.mesh_uri_length),
      .material_uri = detail::scene_text(value.material_uri, value.material_uri_length),
      .local_transform = from_native(value.local_transform),
      .component_flags = static_cast<scene_node_components>(value.component_flags),
      .camera = from_native(value.camera),
  };
}

[[nodiscard]] inline scene_prefab_node_info
from_native(const gneiss_scene_prefab_node_info& value) noexcept {
  return {
      .flags = static_cast<scene_prefab_flags>(value.flags),
      .node = scene_node_id{value.node},
      .parent = scene_node_id{value.parent},
      .entity = entity_id{value.entity},
      .instance_uuid = detail::scene_text(value.instance_uuid, value.instance_uuid_length),
      .source_node_uuid = detail::scene_text(value.source_node_uuid, value.source_node_uuid_length),
      .name = detail::scene_text(value.name, value.name_length),
      .prefab_uri = detail::scene_text(value.prefab_uri, value.prefab_uri_length),
      .local_transform = from_native(value.local_transform),
      .source_local_transform = from_native(value.source_local_transform),
  };
}

} // namespace gneiss

#endif
