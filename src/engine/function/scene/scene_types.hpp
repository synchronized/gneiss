// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/entity_id.hpp"
#include "engine/core/guid.hpp"
#include <compare>
#include <cstdint>
#include <string>
#include <string_view>

namespace gneiss {
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
}

namespace gneiss::engine {
/** 实例化回执中的来源关联；由调用方保存，不是运行对象属性。 */
struct scene_object_binding {
  guid object, source;
  entity_id node;
  entity_id entity;
};
struct scene_node_identity final {
  guid uuid;
  std::string name;
};
struct scene_node_identity_view final {
  guid uuid;
  std::string_view name;
};
}
