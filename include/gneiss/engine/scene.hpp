// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SCENE_HPP_
#define GNEISS_SCENE_HPP_

#include <gneiss/engine/core/entity.hpp>
#include <gneiss/engine/core/result.hpp>
#include <gneiss/engine/render.hpp>
#include <gneiss/engine/scene.h>

#include <array>
#include <concepts>
#include <cstdint>
#include <exception>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace gneiss {

/** 不拥有 Scene Node 的强类型运行时标识。 */
class scene_node_id final {
public:
  constexpr scene_node_id() noexcept = default;
  explicit constexpr scene_node_id(gneiss_scene_node_id value) noexcept : value_(value) {}
  [[nodiscard]] constexpr bool is_valid() const noexcept {
    return value_ != GNEISS_NULL_SCENE_NODE_ID;
  }
  [[nodiscard]] constexpr gneiss_scene_node_id get() const noexcept { return value_; }
  friend constexpr bool operator==(scene_node_id, scene_node_id) noexcept = default;

private:
  gneiss_scene_node_id value_ = GNEISS_NULL_SCENE_NODE_ID;
};

inline constexpr scene_node_id null_scene_node_id{};
/** 右手系变换值；默认零平移、单位四元数与单位缩放，不持有场景资源。 */
struct transform {
  std::array<float, 3> translation{0.0F, 0.0F, 0.0F};
  std::array<float, 4> rotation{0.0F, 0.0F, 0.0F, 1.0F};
  std::array<float, 3> scale{1.0F, 1.0F, 1.0F};
};
/** 显式复制到 C ABI 值，不依赖两种类型的内存布局。 */
[[nodiscard]] constexpr gneiss_transform to_native(const transform& value) noexcept;
/** 从 C ABI 值逐字段复制；几何有效性在设置到场景时校验。 */
[[nodiscard]] constexpr transform from_native(const gneiss_transform& value) noexcept;
// NOLINTNEXTLINE(performance-enum-size): 保留完整标志位宽度。
enum class scene_node_components : std::uint32_t {
  none = 0,
  camera = 1,
  mesh_renderer = 2,
  primary_camera = 4,
};
// NOLINTNEXTLINE(performance-enum-size): 保留完整标志位宽度。
enum class scene_prefab_flags : std::uint32_t {
  none = 0,
  instance_root = 1,
  source_read_only = 2,
  translation_overridden = 4,
  rotation_overridden = 8,
  scale_overridden = 16,
};
template <typename T>
concept scene_flags = std::same_as<T, scene_node_components> || std::same_as<T, scene_prefab_flags>;
template <scene_flags T> [[nodiscard]] constexpr T operator|(T left, T right) noexcept {
  return static_cast<T>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
}
template <scene_flags T> [[nodiscard]] constexpr T operator&(T left, T right) noexcept {
  return static_cast<T>(static_cast<std::uint32_t>(left) & static_cast<std::uint32_t>(right));
}
template <scene_flags T> [[nodiscard]] constexpr bool has_flags(T value, T flags) noexcept {
  return (value & flags) == flags;
}
inline constexpr std::uint64_t scene_subtree_max_nodes = 4096;
// 描述默认值允许 designated 初始化省略字段，避免消费者的缺省字段告警。
// NOLINTBEGIN(readability-redundant-member-init)
/** 值描述；文本借用到下次场景修改或实例/父对象失效，跨修改须自行复制。 */
struct scene_instance_node_info {
  scene_node_id node{};
  scene_node_id parent{};
  entity_id entity{};
  std::string_view uuid{};
  std::string_view name{};
  std::string_view mesh_uri{};
  std::string_view material_uri{};
  transform local_transform{};
  scene_node_components component_flags = scene_node_components::none;
  camera_desc camera{};
};
/** 所有输入文本只在同步创建期间借用。 */
struct scene_node_desc {
  scene_node_id parent{};
  std::string_view uuid{};
  std::string_view name{};
  transform local_transform{};
};
/** 文本借用期同普通节点描述；刷新后节点 ID 也须重新查询。 */
struct scene_prefab_node_info {
  scene_prefab_flags flags = scene_prefab_flags::none;
  scene_node_id node{};
  scene_node_id parent{};
  entity_id entity{};
  std::string_view instance_uuid{};
  std::string_view source_node_uuid{};
  std::string_view name{};
  std::string_view prefab_uri{};
  transform local_transform{};
  transform source_local_transform{};
};
struct scene_prefab_instance_desc {
  scene_node_id parent{};
  std::string_view instance_uuid{};
  std::string_view name{};
  std::string_view prefab_uri{};
  transform local_transform{};
};
struct scene_uuid_mapping {
  std::string_view source_uuid{};
  std::string_view target_uuid{};
};
struct scene_mesh_renderer_desc {
  std::string_view mesh_uri{};
  std::string_view material_uri{};
};
struct scene_camera_desc {
  camera_desc camera{};
  bool is_primary = false;
};
struct scene_mesh_renderer_node_desc {
  scene_node_id parent{};
  std::string_view uuid{};
  std::string_view name{};
  scene_mesh_renderer_desc renderer{};
};
// NOLINTEND(readability-redundant-member-init)
/** 显式 C 互操作；文本仍借用原存储，不延长寿命。 */
[[nodiscard]] inline gneiss_scene_node_desc to_native(const scene_node_desc& value) noexcept;
/** 显式 C 互操作；文本仍借用原存储，不延长寿命。 */
[[nodiscard]] inline gneiss_scene_prefab_instance_desc
to_native(const scene_prefab_instance_desc& value) noexcept;
/** 显式 C 互操作；文本仍借用原存储，不延长寿命。 */
[[nodiscard]] inline gneiss_scene_uuid_mapping to_native(const scene_uuid_mapping& value) noexcept;
/** 显式 C 互操作；文本仍借用原存储，不延长寿命。 */
[[nodiscard]] inline gneiss_scene_mesh_renderer_desc
to_native(const scene_mesh_renderer_desc& value) noexcept;
/** 显式 C 互操作；文本仍借用原存储，不延长寿命。 */
[[nodiscard]] inline gneiss_scene_camera_desc to_native(const scene_camera_desc& value) noexcept;
/** 显式 C 互操作；文本仍借用原存储，不延长寿命。 */
[[nodiscard]] inline gneiss_scene_mesh_renderer_node_desc
to_native(const scene_mesh_renderer_node_desc& value) noexcept;

/** 从有效 C 查询值借用文本；不复制文本内容。 */
[[nodiscard]] inline scene_instance_node_info
from_native(const gneiss_scene_instance_node_info& value) noexcept;
/** 从有效 C 查询值借用文本；不复制文本内容。 */
[[nodiscard]] inline scene_prefab_node_info
from_native(const gneiss_scene_prefab_node_info& value) noexcept;
/** 独占 Prefab 刷新的撤销令牌，不延长 Application 或场景寿命。
 * 操作、析构及移动覆盖限所属 Application 线程；释放只丢弃历史，不撤销当前投影。 */
class scene_prefab_refresh final {
public:
  scene_prefab_refresh() noexcept = default;
  ~scene_prefab_refresh() noexcept { reset_or_terminate(); }
  scene_prefab_refresh(const scene_prefab_refresh&) = delete;
  scene_prefab_refresh& operator=(const scene_prefab_refresh&) = delete;
  scene_prefab_refresh(scene_prefab_refresh&& other) noexcept
      : application_(std::exchange(other.application_, GNEISS_NULL_APPLICATION)),
        scene_(std::exchange(other.scene_, GNEISS_NULL_SCENE_INSTANCE)),
        token_(std::exchange(other.token_, GNEISS_NULL_SCENE_PREFAB_REFRESH_TOKEN)) {}
  scene_prefab_refresh& operator=(scene_prefab_refresh&& other) noexcept {
    if (this != &other) {
      reset_or_terminate();
      application_ = std::exchange(other.application_, GNEISS_NULL_APPLICATION);
      scene_ = std::exchange(other.scene_, GNEISS_NULL_SCENE_INSTANCE);
      token_ = std::exchange(other.token_, GNEISS_NULL_SCENE_PREFAB_REFRESH_TOKEN);
    }
    return *this;
  }
  /** 非零仅表示包装持有令牌，不保证父对象仍存活。 */
  [[nodiscard]] explicit operator bool() const noexcept {
    return token_ != GNEISS_NULL_SCENE_PREFAB_REFRESH_TOKEN;
  }
  [[nodiscard]] gneiss_scene_prefab_refresh_token get() const noexcept { return token_; }
  [[nodiscard]] gneiss_application owner() const noexcept { return application_; }
  [[nodiscard]] gneiss_scene_instance scene() const noexcept { return scene_; }
  /** 在新旧版本间切换；成功后旧节点 ID 失效，失败保留 out_new_root。 */
  [[nodiscard]] result toggle(scene_node_id& out_new_root) const noexcept;
  /** 转移令牌；调用前保存 owner() 和 scene()，接收者负责手动释放。 */
  [[nodiscard]] gneiss_scene_prefab_refresh_token release() noexcept {
    application_ = GNEISS_NULL_APPLICATION;
    scene_ = GNEISS_NULL_SCENE_INSTANCE;
    return std::exchange(token_, GNEISS_NULL_SCENE_PREFAB_REFRESH_TOKEN);
  }
  /** 幂等释放；令牌或父对象已失效视为完成。其他失败保留句柄供重试。
   * 析构和移动覆盖若关闭失败则终止进程。 */
  [[nodiscard]] result reset() noexcept;

private:
  friend class scene_instance;
  void reset_or_terminate() noexcept;
  gneiss_application application_ = GNEISS_NULL_APPLICATION;
  gneiss_scene_instance scene_ = GNEISS_NULL_SCENE_INSTANCE;
  gneiss_scene_prefab_refresh_token token_ = GNEISS_NULL_SCENE_PREFAB_REFRESH_TOKEN;
};

/** 独占拥有已加载场景；只在所属 Application 线程操作，不延长父对象寿命。 */
class scene_instance final {
public:
  scene_instance() noexcept = default;
  ~scene_instance() noexcept { reset_or_terminate(); }

  scene_instance(const scene_instance&) = delete;
  scene_instance& operator=(const scene_instance&) = delete;
  scene_instance(scene_instance&& other) noexcept
      : application_(std::exchange(other.application_, GNEISS_NULL_APPLICATION)),
        handle_(std::exchange(other.handle_, GNEISS_NULL_SCENE_INSTANCE)) {}
  scene_instance& operator=(scene_instance&& other) noexcept {
    if (this != &other) {
      reset_or_terminate();
      application_ = std::exchange(other.application_, GNEISS_NULL_APPLICATION);
      handle_ = std::exchange(other.handle_, GNEISS_NULL_SCENE_INSTANCE);
    }
    return *this;
  }

  [[nodiscard]] static result load(gneiss_application application, std::string_view uri,
                                   scene_instance& out_instance) noexcept;

  [[nodiscard]] static result create_empty(gneiss_application application,
                                           std::string_view scene_uuid,
                                           scene_instance& out_instance) noexcept;

  [[nodiscard]] bool is_valid() const noexcept { return handle_ != GNEISS_NULL_SCENE_INSTANCE; }
  [[nodiscard]] gneiss_scene_instance get() const noexcept { return handle_; }
  [[nodiscard]] result find_node(std::string_view uuid, scene_node_id& out_node) const noexcept;
  [[nodiscard]] result get_node_count(std::uint64_t& out_count) const noexcept;
  /** 描述按值输出，但字符串只借用到下次场景修改或父对象失效；跨修改使用前须复制。 */
  [[nodiscard]] result get_node_info(std::uint64_t index,
                                     scene_instance_node_info& out_info) const noexcept;
  [[nodiscard]] result get_prefab_node_count(std::uint64_t& out_count) const noexcept;
  /** Prefab 描述的字符串借用期限与 get_node_info 相同；刷新后节点 ID 也须重新查询。 */
  [[nodiscard]] result get_prefab_node_info(std::uint64_t index,
                                            scene_prefab_node_info& out_info) const noexcept;
  [[nodiscard]] result create_prefab_instance(const scene_prefab_instance_desc& desc,
                                              scene_node_id& out_root) const noexcept;
  [[nodiscard]] result set_prefab_instance_name(scene_node_id root,
                                                std::string_view name) const noexcept;
  [[nodiscard]] result set_prefab_source_transform(scene_node_id node,
                                                   const transform& value) const noexcept;
  [[nodiscard]] result destroy_prefab_instance(scene_node_id root) const noexcept;
  [[nodiscard]] result
  refresh_prefab_instance_native(scene_node_id root, scene_node_id& out_new_root,
                                 gneiss_scene_prefab_refresh_token& out_token) const noexcept;
  /** 刷新并接管撤销令牌；out_refresh 必须为空，否则返回 invalid_state 且不刷新。
   * 失败不改变场景及两个输出；成功后由 out_refresh 自动释放历史租约。 */
  [[nodiscard]] result refresh_prefab_instance(scene_node_id root, scene_node_id& out_new_root,
                                               scene_prefab_refresh& out_refresh) const noexcept;
  [[nodiscard]] result toggle_prefab_refresh_native(gneiss_scene_prefab_refresh_token token,
                                                    scene_node_id& out_new_root) const noexcept;
  [[nodiscard]] result
  release_prefab_refresh_native(gneiss_scene_prefab_refresh_token token) const noexcept;
  [[nodiscard]] result create_mesh_renderer_node(const scene_mesh_renderer_node_desc& desc,
                                                 scene_node_id& out_node) const noexcept;
  [[nodiscard]] result create_node(const scene_node_desc& desc,
                                   scene_node_id& out_node) const noexcept;
  [[nodiscard]] result set_node_name(scene_node_id node, std::string_view name) const noexcept;
  [[nodiscard]] result reparent_node(scene_node_id node, scene_node_id parent) const noexcept;
  [[nodiscard]] result capture_subtree(scene_node_id root,
                                       std::string& out_snapshot) const noexcept;
  [[nodiscard]] result restore_subtree(std::string_view snapshot, scene_node_id parent,
                                       std::span<const scene_uuid_mapping> mappings,
                                       scene_node_id& out_root) const noexcept;
  [[nodiscard]] result destroy_subtree(scene_node_id root) const noexcept;
  [[nodiscard]] result set_mesh_renderer(scene_node_id node,
                                         const scene_mesh_renderer_desc& desc) const noexcept;
  [[nodiscard]] result set_camera(scene_node_id node, const scene_camera_desc& desc) const noexcept;
  [[nodiscard]] result remove_camera(scene_node_id node) const noexcept;
  [[nodiscard]] result remove_mesh_renderer(scene_node_id node) const noexcept;
  [[nodiscard]] result destroy_node(scene_node_id node) const noexcept;
  [[nodiscard]] result serialize(std::string& out_json) const noexcept;
  /** 所属 Application 的非拥有句柄；release() 前保存以便手动卸载。 */
  [[nodiscard]] gneiss_application owner() const noexcept { return application_; }
  /** 转移场景所有权；调用方负责使用原 owner() 在所属线程卸载。 */
  [[nodiscard]] gneiss_scene_instance release() noexcept {
    application_ = GNEISS_NULL_APPLICATION;
    return std::exchange(handle_, GNEISS_NULL_SCENE_INSTANCE);
  }
  /** 幂等卸载；父对象或场景已失效视为释放完成，其他失败保留句柄供重试。
   * 析构或移动覆盖若关闭失败则终止进程；不会隐式跨线程卸载。 */
  result reset() noexcept;

private:
  void reset_or_terminate() noexcept;
  gneiss_application application_ = GNEISS_NULL_APPLICATION;
  gneiss_scene_instance handle_ = GNEISS_NULL_SCENE_INSTANCE;
};

} // namespace gneiss

#include <gneiss/engine/detail/scene.inl>

#endif
