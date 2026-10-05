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

#include <exception>
#include <utility>

namespace gneiss {

/** 内建 Transform 的稳定类型标识，值不依赖 World 生命周期。 */
[[nodiscard]] inline gneiss_type_id transform_type_id() noexcept {
  return gneiss_transform_type_id();
}
/** 内建 Camera 的稳定类型标识，值不依赖 World 生命周期。 */
[[nodiscard]] inline gneiss_type_id camera_type_id() noexcept { return gneiss_camera_type_id(); }

/** 非拥有 World 视图；销毁视图不销毁 World，父对象失效后操作返回无效句柄。
 * 仅在 World 所属线程调用；is_valid() 只检查本地非零值，不探测存活状态。 */
class world_ref {
public:
  world_ref() noexcept = default;
  explicit world_ref(gneiss_world handle) noexcept : handle_(handle) {}
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

  /** 查询存活实体数；须在 World 所属线程调用。 */
  [[nodiscard]] result entity_count(std::uint64_t& output) const noexcept {
    return from_native(gneiss_world_entity_count(handle_, &output));
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

  /** 读取节点局部变换，不转移节点所有权。 */
  [[nodiscard]] result get_local_transform(scene_node_id node, transform& output) const noexcept {
    return from_native(gneiss_scene_node_get_local_transform(handle_, node.get(), &output));
  }
  /** 查询节点关联实体；失败时不改变输出。 */
  [[nodiscard]] result get_entity(scene_node_id node, entity_id& output) const noexcept {
    gneiss_entity_id value{};
    const auto status = from_native(gneiss_scene_node_get_entity(handle_, node.get(), &value));
    if (status.ok()) {
      output = entity_id{value};
    }
    return status;
  }
  /** 查询父节点；根节点成功返回空标识，失败时不改变输出。 */
  [[nodiscard]] result get_parent(scene_node_id node, scene_node_id& output) const noexcept {
    gneiss_scene_node_id value{};
    const auto status = from_native(gneiss_scene_node_get_parent(handle_, node.get(), &value));
    if (status.ok()) {
      output = scene_node_id{value};
    }
    return status;
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

protected:
  gneiss_world handle_ = GNEISS_NULL_WORLD;
};

/** 独占拥有一个 World 的 RAII 包装；只允许在创建线程访问。
 * 析构或移动覆盖若关闭失败则终止进程；需处理错误时先显式 reset()。 */
class world final : private world_ref {
public:
  using world_ref::configure_camera;
  using world_ref::create_entity;
  using world_ref::create_scene_node;
  using world_ref::destroy_entity;
  using world_ref::destroy_scene_node;
  using world_ref::entity_count;
  using world_ref::get;
  using world_ref::get_active_camera;
  using world_ref::get_camera;
  using world_ref::get_entity;
  using world_ref::get_local_transform;
  using world_ref::get_parent;
  using world_ref::get_world_transform;
  using world_ref::is_alive;
  using world_ref::is_valid;
  using world_ref::register_reflection;
  using world_ref::remove_camera;
  using world_ref::remove_mesh_renderer;
  using world_ref::reparent_scene_node;
  using world_ref::set_active_camera;
  using world_ref::set_camera;
  using world_ref::set_local_transform;
  using world_ref::set_mesh_renderer;

  world() noexcept = default;
  ~world() noexcept { reset_or_terminate(); }

  world(const world&) = delete;
  world& operator=(const world&) = delete;

  world(world&& other) noexcept : world_ref(std::exchange(other.handle_, GNEISS_NULL_WORLD)) {}
  world& operator=(world&& other) noexcept {
    if (this != &other) {
      reset_or_terminate();
      handle_ = std::exchange(other.handle_, GNEISS_NULL_WORLD);
    }
    return *this;
  }

  [[nodiscard]] static result create(world& out_world) noexcept {
    const gneiss_world_desc desc = GNEISS_WORLD_DESC_INIT;
    gneiss_world handle = GNEISS_NULL_WORLD;
    const auto native_result = gneiss_world_create(&desc, &handle);
    if (native_result == GNEISS_SUCCESS) {
      world candidate;
      candidate.handle_ = handle;
      const auto closed = out_world.reset();
      if (closed.failed()) {
        return closed;
      }
      out_world.handle_ = candidate.release();
    }
    return from_native(native_result);
  }

  /** 返回不拥有 World 的视图，不延长本对象生命周期。 */
  [[nodiscard]] world_ref ref() const noexcept { return world_ref{handle_}; }
  /** 幂等关闭；无效句柄视为已释放，其他失败保留句柄供所属线程重试。
   * 返回结果可供检查；保留直接 reset() 的既有调用方式。 */
  result reset() noexcept {
    if (handle_ == GNEISS_NULL_WORLD) {
      return result::success;
    }
    const auto status = from_native(gneiss_world_destroy(handle_));
    if (status.failed() && status != result::invalid_handle) {
      return status;
    }
    handle_ = GNEISS_NULL_WORLD;
    return result::success;
  }
  /** 转移原始句柄所有权，调用方负责在所属线程销毁。 */
  [[nodiscard]] gneiss_world release() noexcept {
    return std::exchange(handle_, GNEISS_NULL_WORLD);
  }

private:
  void reset_or_terminate() noexcept {
    if (reset().failed()) {
      std::terminate();
    }
  }
};

} // namespace gneiss

#endif
