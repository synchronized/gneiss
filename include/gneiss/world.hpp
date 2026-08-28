// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_WORLD_HPP_
#define GNEISS_WORLD_HPP_

#include <gneiss/core/entity.hpp>
#include <gneiss/core/result.hpp>
#include <gneiss/reflection.hpp>
#include <gneiss/render.hpp>
#include <gneiss/scene.hpp>
#include <gneiss/world.h>

#include <utility>

namespace gneiss {

/** 独占拥有一个 World 的 RAII 包装；只允许在创建线程访问。 */
class world final {
public:
  world() noexcept = default;
  ~world() noexcept { reset(); }

  world(const world&) = delete;
  world& operator=(const world&) = delete;

  world(world&& other) noexcept : handle_(std::exchange(other.handle_, GNEISS_NULL_WORLD)) {}
  world& operator=(world&& other) noexcept {
    if (this != &other) {
      reset();
      handle_ = std::exchange(other.handle_, GNEISS_NULL_WORLD);
    }
    return *this;
  }

  [[nodiscard]] static result create(world& out_world) noexcept {
    const gneiss_world_desc desc = GNEISS_WORLD_DESC_INIT;
    gneiss_world handle = GNEISS_NULL_WORLD;
    const auto native_result = gneiss_world_create(&desc, &handle);
    if (native_result == GNEISS_SUCCESS) {
      out_world.reset();
      out_world.handle_ = handle;
    }
    return from_native(native_result);
  }

  [[nodiscard]] bool is_valid() const noexcept { return handle_ != GNEISS_NULL_WORLD; }
  [[nodiscard]] gneiss_world get() const noexcept { return handle_; }

  [[nodiscard]] static result register_reflection(type_registry& registry) noexcept {
    return from_native(gneiss_world_register_reflection(registry.get()));
  }

  [[nodiscard]] result create_entity(entity_id& out_entity) noexcept {
    gneiss_entity_id native_entity = GNEISS_NULL_ENTITY_ID;
    const auto native_result = gneiss_world_entity_create(handle_, &native_entity);
    if (native_result == GNEISS_SUCCESS) {
      out_entity = entity_id{native_entity};
    }
    return from_native(native_result);
  }

  [[nodiscard]] result destroy_entity(entity_id entity) noexcept {
    return from_native(gneiss_world_entity_destroy(handle_, entity.get()));
  }

  [[nodiscard]] result set_camera(entity_id entity, const camera& value) noexcept {
    return from_native(gneiss_world_entity_set_camera(handle_, entity.get(), &value));
  }

  [[nodiscard]] result configure_camera(entity_id entity, const camera_desc& value) noexcept {
    return from_native(gneiss_world_entity_configure_camera(handle_, entity.get(), &value));
  }

  [[nodiscard]] result get_camera(entity_id entity, camera_desc& out_camera) const noexcept {
    return from_native(gneiss_world_entity_get_camera(handle_, entity.get(), &out_camera));
  }

  [[nodiscard]] result remove_camera(entity_id entity) noexcept {
    return from_native(gneiss_world_entity_remove_camera(handle_, entity.get()));
  }

  [[nodiscard]] result set_active_camera(entity_id entity) noexcept {
    return from_native(gneiss_world_set_active_camera(handle_, entity.get()));
  }

  [[nodiscard]] result get_active_camera(entity_id& out_entity) const noexcept {
    gneiss_entity_id native_entity = GNEISS_NULL_ENTITY_ID;
    const auto native_result = gneiss_world_get_active_camera(handle_, &native_entity);
    out_entity = entity_id{native_entity};
    return from_native(native_result);
  }

  [[nodiscard]] result set_mesh_renderer(entity_id entity, const mesh_renderer& value) noexcept {
    return from_native(gneiss_world_entity_set_mesh_renderer(handle_, entity.get(), &value));
  }
  [[nodiscard]] result remove_mesh_renderer(entity_id entity) noexcept {
    return from_native(gneiss_world_entity_remove_mesh_renderer(handle_, entity.get()));
  }

  [[nodiscard]] result create_scene_node(scene_node_id parent, entity_id entity,
                                         scene_node_id& out_node) noexcept {
    gneiss_scene_node_id native_node = GNEISS_NULL_SCENE_NODE_ID;
    const auto native_result =
        gneiss_scene_node_create(handle_, parent.get(), entity.get(), &native_node);
    if (native_result == GNEISS_SUCCESS) {
      out_node = scene_node_id{native_node};
    }
    return from_native(native_result);
  }

  [[nodiscard]] result destroy_scene_node(scene_node_id node) noexcept {
    return from_native(gneiss_scene_node_destroy(handle_, node.get()));
  }

  [[nodiscard]] result reparent_scene_node(scene_node_id node, scene_node_id parent) noexcept {
    return from_native(gneiss_scene_node_reparent(handle_, node.get(), parent.get()));
  }

  [[nodiscard]] result set_local_transform(scene_node_id node, const transform& value) noexcept {
    return from_native(gneiss_scene_node_set_local_transform(handle_, node.get(), &value));
  }

  [[nodiscard]] result set_local_transform(entity_id entity, const transform& value) noexcept {
    return from_native(gneiss_world_entity_set_local_transform(handle_, entity.get(), &value));
  }

  [[nodiscard]] result get_local_transform(entity_id entity, transform& output) const noexcept {
    return from_native(gneiss_world_entity_get_local_transform(handle_, entity.get(), &output));
  }

  [[nodiscard]] result get_world_transform(scene_node_id node,
                                           transform& out_transform) const noexcept {
    return from_native(gneiss_scene_node_get_world_transform(handle_, node.get(), &out_transform));
  }

  [[nodiscard]] result is_alive(entity_id entity, bool& out_is_alive) const noexcept {
    uint8_t native_is_alive = 0;
    const auto native_result =
        gneiss_world_entity_is_alive(handle_, entity.get(), &native_is_alive);
    if (native_result == GNEISS_SUCCESS) {
      out_is_alive = native_is_alive != 0;
    }
    return from_native(native_result);
  }

  void reset() noexcept {
    if (handle_ != GNEISS_NULL_WORLD) {
      (void)gneiss_world_destroy(handle_);
      handle_ = GNEISS_NULL_WORLD;
    }
  }

private:
  gneiss_world handle_ = GNEISS_NULL_WORLD;
};

} // namespace gneiss

#endif
