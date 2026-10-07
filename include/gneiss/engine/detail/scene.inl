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

inline result scene_prefab_refresh::toggle(scene_node_id& out_new_root) const noexcept {
  gneiss_scene_node_id root = GNEISS_NULL_SCENE_NODE_ID;
  const auto status =
      from_native(gneiss_scene_instance_toggle_prefab_refresh(application_, scene_, token_, &root));
  if (status.ok()) {
    out_new_root = scene_node_id{root};
  }
  return status;
}

inline result scene_prefab_refresh::reset() noexcept {
  if (!*this) {
    return result::success;
  }
  const auto status =
      from_native(gneiss_scene_instance_release_prefab_refresh(application_, scene_, token_));
  if (status.failed() && status != result::invalid_handle) {
    return status;
  }
  (void)release();
  return result::success;
}

inline void scene_prefab_refresh::reset_or_terminate() noexcept {
  if (reset().failed()) {
    std::terminate();
  }
}

inline result scene_instance::load(gneiss_application application, std::string_view uri,
                                   scene_instance& out_instance) noexcept {
  gneiss_scene_instance handle = GNEISS_NULL_SCENE_INSTANCE;
  const auto native_result =
      gneiss_scene_instance_load(application, uri.data(), uri.size(), &handle);
  if (native_result == GNEISS_SUCCESS) {
    scene_instance candidate;
    candidate.application_ = application;
    candidate.handle_ = handle;
    const auto closed = out_instance.reset();
    if (closed.failed()) {
      return closed;
    }
    out_instance = std::move(candidate);
  }
  return from_native(native_result);
}

inline result scene_instance::create_empty(gneiss_application application,
                                           std::string_view scene_uuid,
                                           scene_instance& out_instance) noexcept {
  gneiss_scene_instance handle = GNEISS_NULL_SCENE_INSTANCE;
  const auto native_result = gneiss_scene_instance_create_empty(application, scene_uuid.data(),
                                                                scene_uuid.size(), &handle);
  if (native_result == GNEISS_SUCCESS) {
    scene_instance candidate;
    candidate.application_ = application;
    candidate.handle_ = handle;
    const auto closed = out_instance.reset();
    if (closed.failed()) {
      return closed;
    }
    out_instance = std::move(candidate);
  }
  return from_native(native_result);
}

inline result scene_instance::find_node(std::string_view uuid,
                                        scene_node_id& out_node) const noexcept {
  gneiss_scene_node_id node = GNEISS_NULL_SCENE_NODE_ID;
  const auto native_result =
      gneiss_scene_instance_find_node(application_, handle_, uuid.data(), uuid.size(), &node);
  if (native_result == GNEISS_SUCCESS) {
    out_node = scene_node_id{node};
  }
  return from_native(native_result);
}

inline result scene_instance::get_node_count(std::uint64_t& out_count) const noexcept {
  return from_native(gneiss_scene_instance_get_node_count(application_, handle_, &out_count));
}

inline result scene_instance::get_node_info(std::uint64_t index,
                                            scene_instance_node_info& out_info) const noexcept {
  gneiss_scene_instance_node_info native = GNEISS_SCENE_INSTANCE_NODE_INFO_INIT;
  const auto status =
      from_native(gneiss_scene_instance_get_node_info(application_, handle_, index, &native));
  if (status.ok()) {
    out_info = from_native(native);
  }
  return status;
}

inline result scene_instance::get_prefab_node_count(std::uint64_t& out_count) const noexcept {
  return from_native(
      gneiss_scene_instance_get_prefab_node_count(application_, handle_, &out_count));
}

inline result
scene_instance::get_prefab_node_info(std::uint64_t index,
                                     scene_prefab_node_info& out_info) const noexcept {
  gneiss_scene_prefab_node_info native = GNEISS_SCENE_PREFAB_NODE_INFO_INIT;
  const auto status = from_native(
      gneiss_scene_instance_get_prefab_node_info(application_, handle_, index, &native));
  if (status.ok()) {
    out_info = from_native(native);
  }
  return status;
}

inline result scene_instance::create_prefab_instance(const scene_prefab_instance_desc& desc,
                                                     scene_node_id& out_root) const noexcept {
  gneiss_scene_node_id root = GNEISS_NULL_SCENE_NODE_ID;
  const auto native = to_native(desc);
  const auto native_result =
      gneiss_scene_instance_create_prefab_instance(application_, handle_, &native, &root);
  if (native_result == GNEISS_SUCCESS) {
    out_root = scene_node_id{root};
  }
  return from_native(native_result);
}

inline result scene_instance::set_prefab_instance_name(scene_node_id root,
                                                       std::string_view name) const noexcept {
  return from_native(gneiss_scene_instance_set_prefab_instance_name(
      application_, handle_, root.get(), name.data(), name.size()));
}

inline result scene_instance::set_prefab_source_transform(scene_node_id node,
                                                          const transform& value) const noexcept {
  const auto native = to_native(value);
  return from_native(gneiss_scene_instance_set_prefab_source_transform(application_, handle_,
                                                                       node.get(), &native));
}

inline result scene_instance::destroy_prefab_instance(scene_node_id root) const noexcept {
  return from_native(
      gneiss_scene_instance_destroy_prefab_instance(application_, handle_, root.get()));
}

inline result scene_instance::refresh_prefab_instance_native(
    scene_node_id root, scene_node_id& out_new_root,
    gneiss_scene_prefab_refresh_token& out_token) const noexcept {
  gneiss_scene_node_id new_root = GNEISS_NULL_SCENE_NODE_ID;
  gneiss_scene_prefab_refresh_token token = GNEISS_NULL_SCENE_PREFAB_REFRESH_TOKEN;
  const auto native_result = gneiss_scene_instance_refresh_prefab_instance(
      application_, handle_, root.get(), &new_root, &token);
  if (native_result == GNEISS_SUCCESS) {
    out_new_root = scene_node_id{new_root};
    out_token = token;
  }
  return from_native(native_result);
}

inline result
scene_instance::refresh_prefab_instance(scene_node_id root, scene_node_id& out_new_root,
                                        scene_prefab_refresh& out_refresh) const noexcept {
  if (out_refresh) {
    return result::invalid_state;
  }
  gneiss_scene_prefab_refresh_token token = GNEISS_NULL_SCENE_PREFAB_REFRESH_TOKEN;
  const auto status = refresh_prefab_instance_native(root, out_new_root, token);
  if (status.ok()) {
    out_refresh.application_ = application_;
    out_refresh.scene_ = handle_;
    out_refresh.token_ = token;
  }
  return status;
}

inline result
scene_instance::toggle_prefab_refresh_native(gneiss_scene_prefab_refresh_token token,
                                             scene_node_id& out_new_root) const noexcept {
  gneiss_scene_node_id new_root = GNEISS_NULL_SCENE_NODE_ID;
  const auto native_result =
      gneiss_scene_instance_toggle_prefab_refresh(application_, handle_, token, &new_root);
  if (native_result == GNEISS_SUCCESS) {
    out_new_root = scene_node_id{new_root};
  }
  return from_native(native_result);
}

inline result scene_instance::release_prefab_refresh_native(
    gneiss_scene_prefab_refresh_token token) const noexcept {
  return from_native(gneiss_scene_instance_release_prefab_refresh(application_, handle_, token));
}

inline result scene_instance::create_mesh_renderer_node(const scene_mesh_renderer_node_desc& desc,
                                                        scene_node_id& out_node) const noexcept {
  gneiss_scene_node_id node = GNEISS_NULL_SCENE_NODE_ID;
  const auto native = to_native(desc);
  const auto native_result =
      gneiss_scene_instance_create_mesh_renderer_node(application_, handle_, &native, &node);
  if (native_result == GNEISS_SUCCESS) {
    out_node = scene_node_id{node};
  }
  return from_native(native_result);
}

inline result scene_instance::create_node(const scene_node_desc& desc,
                                          scene_node_id& out_node) const noexcept {
  gneiss_scene_node_id node = GNEISS_NULL_SCENE_NODE_ID;
  const auto native = to_native(desc);
  const auto native_result =
      gneiss_scene_instance_create_node(application_, handle_, &native, &node);
  if (native_result == GNEISS_SUCCESS) {
    out_node = scene_node_id{node};
  }
  return from_native(native_result);
}

inline result scene_instance::set_node_name(scene_node_id node,
                                            std::string_view name) const noexcept {
  return from_native(gneiss_scene_instance_set_node_name(application_, handle_, node.get(),
                                                         name.data(), name.size()));
}

inline result scene_instance::reparent_node(scene_node_id node,
                                            scene_node_id parent) const noexcept {
  return from_native(
      gneiss_scene_instance_reparent_node(application_, handle_, node.get(), parent.get()));
}

inline result scene_instance::capture_subtree(scene_node_id root,
                                              std::string& out_snapshot) const noexcept {
  std::uint64_t length = 0;
  auto native_result = gneiss_scene_instance_capture_subtree(application_, handle_, root.get(),
                                                             nullptr, 0U, &length);
  if (native_result != GNEISS_SUCCESS) {
    return from_native(native_result);
  }
  if (length > std::numeric_limits<std::size_t>::max()) {
    return result::out_of_memory;
  }
  try {
    std::string value(static_cast<std::size_t>(length), '\0');
    native_result = gneiss_scene_instance_capture_subtree(application_, handle_, root.get(),
                                                          value.data(), value.size(), &length);
    if (native_result == GNEISS_SUCCESS) {
      out_snapshot = std::move(value);
    }
    return from_native(native_result);
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

inline result scene_instance::restore_subtree(std::string_view snapshot, scene_node_id parent,
                                              std::span<const scene_uuid_mapping> mappings,
                                              scene_node_id& out_root) const noexcept {
  try {
    std::vector<gneiss_scene_uuid_mapping> native;
    native.reserve(mappings.size());
    for (const auto& mapping : mappings) {
      native.push_back(to_native(mapping));
    }
    gneiss_scene_node_id root = GNEISS_NULL_SCENE_NODE_ID;
    const auto status = from_native(gneiss_scene_instance_restore_subtree(
        application_, handle_, snapshot.data(), snapshot.size(), parent.get(), native.data(),
        native.size(), &root));
    if (status.ok()) {
      out_root = scene_node_id{root};
    }
    return status;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (const std::length_error&) {
    return result::invalid_argument;
  }
}

inline result scene_instance::destroy_subtree(scene_node_id root) const noexcept {
  return from_native(gneiss_scene_instance_destroy_subtree(application_, handle_, root.get()));
}

inline result
scene_instance::set_mesh_renderer(scene_node_id node,
                                  const scene_mesh_renderer_desc& desc) const noexcept {
  const auto native = to_native(desc);
  return from_native(
      gneiss_scene_instance_set_mesh_renderer(application_, handle_, node.get(), &native));
}

inline result scene_instance::set_camera(scene_node_id node,
                                         const scene_camera_desc& desc) const noexcept {
  const auto native = to_native(desc);
  return from_native(gneiss_scene_instance_set_camera(application_, handle_, node.get(), &native));
}

inline result scene_instance::remove_camera(scene_node_id node) const noexcept {
  return from_native(gneiss_scene_instance_remove_camera(application_, handle_, node.get()));
}

inline result scene_instance::remove_mesh_renderer(scene_node_id node) const noexcept {
  return from_native(gneiss_scene_instance_remove_mesh_renderer(application_, handle_, node.get()));
}

inline result scene_instance::destroy_node(scene_node_id node) const noexcept {
  return from_native(gneiss_scene_instance_destroy_node(application_, handle_, node.get()));
}

inline result scene_instance::serialize(std::string& out_json) const noexcept {
  std::uint64_t length = 0;
  auto native_result = gneiss_scene_instance_serialize(application_, handle_, nullptr, 0U, &length);
  if (native_result != GNEISS_SUCCESS) {
    return from_native(native_result);
  }
  if (length > std::numeric_limits<std::size_t>::max()) {
    return result::out_of_memory;
  }
  try {
    std::string value(static_cast<std::size_t>(length), '\0');
    native_result =
        gneiss_scene_instance_serialize(application_, handle_, value.data(), value.size(), &length);
    if (native_result == GNEISS_SUCCESS) {
      out_json = std::move(value);
    }
    return from_native(native_result);
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

inline result scene_instance::reset() noexcept {
  if (handle_ == GNEISS_NULL_SCENE_INSTANCE) {
    return result::success;
  }
  const auto status = from_native(gneiss_scene_instance_unload(application_, handle_));
  if (status.failed() && status != result::invalid_handle) {
    return status;
  }
  handle_ = GNEISS_NULL_SCENE_INSTANCE;
  application_ = GNEISS_NULL_APPLICATION;
  return result::success;
}

inline void scene_instance::reset_or_terminate() noexcept {
  if (reset().failed()) {
    std::terminate();
  }
}

} // namespace gneiss

#endif
