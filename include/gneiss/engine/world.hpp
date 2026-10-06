// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_WORLD_HPP_
#define GNEISS_WORLD_HPP_

#include <gneiss/engine/core/entity.hpp>
#include <gneiss/engine/core/result.hpp>
#include <gneiss/engine/reflection.hpp>
#include <gneiss/engine/render.hpp>
#include <gneiss/engine/scene.hpp>
#include <gneiss/engine/world.h>

#include <exception>
#include <utility>

namespace gneiss {

/** 内建 Transform 的稳定类型标识，值不依赖 World 生命周期。 */
[[nodiscard]] inline type_id transform_type_id() noexcept {
  return from_native(gneiss_transform_type_id());
}
/** 内建 Camera 的稳定类型标识，值不依赖 World 生命周期。 */
[[nodiscard]] inline type_id camera_type_id() noexcept {
  return from_native(gneiss_camera_type_id());
}

namespace transform_fields {
inline constexpr field_id translation{1};
inline constexpr field_id rotation{2};
inline constexpr field_id scale{3};
} // namespace transform_fields
namespace camera_fields {
inline constexpr field_id vertical_field_of_view_radians{1};
inline constexpr field_id near_plane{2};
inline constexpr field_id far_plane{3};
inline constexpr field_id is_primary{4};
} // namespace camera_fields

/** 非拥有 World 视图；销毁视图不销毁 World，父对象失效后操作返回无效句柄。
 * const 只约束借用视图本身，不表示 World 只读。
 * 仅在 World 所属线程调用；is_valid() 只检查本地非零值，不探测存活状态。 */
class world_ref {
public:
  world_ref() noexcept = default;
  explicit world_ref(gneiss_world handle) noexcept : handle_(handle) {}
  [[nodiscard]] bool is_valid() const noexcept { return handle_ != GNEISS_NULL_WORLD; }
  [[nodiscard]] gneiss_world get() const noexcept { return handle_; }

  [[nodiscard]] static result register_reflection(type_registry& registry) noexcept;

  /** 创建实体身份，不自动创建场景节点或变换；失败保留输出。 */
  [[nodiscard]] result create_entity(entity_id& out_entity) const noexcept;

  /** 查询存活实体数；须在 World 所属线程调用。 */
  [[nodiscard]] result entity_count(std::uint64_t& output) const noexcept;

  [[nodiscard]] result destroy_entity(entity_id entity) const noexcept;

  [[nodiscard]] result set_camera(entity_id entity, const camera& value) const noexcept;

  [[nodiscard]] result configure_camera(entity_id entity, const camera_desc& value) const noexcept;

  [[nodiscard]] result get_camera(entity_id entity, camera_desc& out_camera) const noexcept;

  [[nodiscard]] result remove_camera(entity_id entity) const noexcept;

  [[nodiscard]] result set_active_camera(entity_id entity) const noexcept;

  /** 返回借用的活动相机实体；未设置返回 not_ready。失败清空输出，保持既有 C 契约。 */
  [[nodiscard]] result get_active_camera(entity_id& out_entity) const noexcept;

  [[nodiscard]] result set_mesh_renderer(entity_id entity,
                                         const mesh_renderer& value) const noexcept;
  [[nodiscard]] result remove_mesh_renderer(entity_id entity) const noexcept;

  [[nodiscard]] result create_scene_node(scene_node_id parent, entity_id entity,
                                         scene_node_id& out_node) const noexcept;

  [[nodiscard]] result destroy_scene_node(scene_node_id node) const noexcept;

  [[nodiscard]] result reparent_scene_node(scene_node_id node, scene_node_id parent) const noexcept;

  [[nodiscard]] result set_local_transform(scene_node_id node,
                                           const transform& value) const noexcept;

  /** 实体必须已关联场景节点；未关联返回 not_found，不隐式创建节点。 */
  [[nodiscard]] result set_local_transform(entity_id entity, const transform& value) const noexcept;

  /** 读取节点局部变换，不转移节点所有权。 */
  [[nodiscard]] result get_local_transform(scene_node_id node, transform& output) const noexcept;
  /** 查询节点关联实体；失败时不改变输出。 */
  [[nodiscard]] result get_entity(scene_node_id node, entity_id& output) const noexcept;
  /** 查询父节点；根节点成功返回空标识，失败时不改变输出。 */
  [[nodiscard]] result get_parent(scene_node_id node, scene_node_id& output) const noexcept;

  /** 实体未关联场景节点时返回 not_found；失败保留输出。 */
  [[nodiscard]] result get_local_transform(entity_id entity, transform& output) const noexcept;

  [[nodiscard]] result get_world_transform(scene_node_id node,
                                           transform& out_transform) const noexcept;

  [[nodiscard]] result is_alive(entity_id entity, bool& out_is_alive) const noexcept;

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

  [[nodiscard]] static result create(world& out_world) noexcept;

  /** 返回不拥有 World 的视图，不延长本对象生命周期。 */
  [[nodiscard]] world_ref ref() const noexcept { return world_ref{handle_}; }
  /** 幂等关闭；无效句柄视为已释放，其他失败保留句柄供所属线程重试。
   * 返回结果可供检查；保留直接 reset() 的既有调用方式。 */
  result reset() noexcept;
  /** 转移原始句柄所有权，调用方负责在所属线程销毁。 */
  [[nodiscard]] gneiss_world release() noexcept {
    return std::exchange(handle_, GNEISS_NULL_WORLD);
  }

private:
  void reset_or_terminate() noexcept;
};

} // namespace gneiss

// 实现随 SDK 安装；使用者只需包含本模块头。
#include <gneiss/engine/detail/world.inl>

#endif
