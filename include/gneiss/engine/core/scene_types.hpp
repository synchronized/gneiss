// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include <concepts>
#include <cstdint>
#include <gneiss/engine/scene.h>

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
template <scene_flags T> constexpr T& operator|=(T& left, T right) noexcept {
  left = left | right;
  return left;
}

template <scene_flags T> [[nodiscard]] constexpr bool has_flags(T value, T flags) noexcept {
  return (value & flags) == flags;
}

}
