// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/guid.hpp"
#include <compare>
#include <concepts>
#include <cstdint>
#include <string>
#include <string_view>

namespace gneiss {
/** 不拥有 Scene Node 的强类型运行时标识。 */
class scene_node_id final {
public:
  constexpr scene_node_id() noexcept = default;
  explicit constexpr scene_node_id(std::uint64_t value) noexcept : value_(value) {}
  explicit constexpr operator bool() const noexcept { return value_ != 0U; }
  [[nodiscard]] constexpr bool is_valid() const noexcept { return value_ != 0; }
  [[nodiscard]] constexpr std::uint64_t get() const noexcept { return value_; }
  friend constexpr auto operator<=>(scene_node_id, scene_node_id) noexcept = default;

private:
  std::uint64_t value_ = 0;
};

inline constexpr scene_node_id null_scene_node_id{};
/** 非拥有的运行期标识；不能与节点或渲染资源句柄混用。 */
class scene_instance_id final {
public:
  constexpr scene_instance_id() noexcept = default;
  explicit constexpr scene_instance_id(std::uint64_t value) noexcept : value_(value) {}
  explicit constexpr operator bool() const noexcept { return value_ != 0U; }
  [[nodiscard]] constexpr bool is_valid() const noexcept { return value_ != 0U; }
  [[nodiscard]] constexpr std::uint64_t get() const noexcept { return value_; }
  friend constexpr auto operator<=>(scene_instance_id, scene_instance_id) noexcept = default;

private:
  std::uint64_t value_{};
};

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

namespace gneiss::engine {
inline constexpr std::uint64_t scene_subtree_max_nodes = 4096;
}

namespace gneiss::engine {
struct scene_node_identity final {
  guid uuid;
  std::string name;
};
struct scene_node_identity_view final {
  guid uuid;
  std::string_view name;
};
}
