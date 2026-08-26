// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SCENE_HPP_
#define GNEISS_SCENE_HPP_

#include <gneiss/core/entity.hpp>
#include <gneiss/core/result.hpp>
#include <gneiss/scene.h>

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
using transform = gneiss_transform;

} // namespace gneiss

#endif
