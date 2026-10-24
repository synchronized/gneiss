// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SCENE_HPP_
#define GNEISS_SCENE_HPP_

#include "engine/core/entity_id.hpp"
#include "engine/core/scene_types.hpp"
#include "engine/values/core/result.hpp"
#include "engine/values/render.hpp"
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

} // namespace gneiss

#include "engine/values/detail/scene.inl"

#endif
