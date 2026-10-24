// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_WORLD_HPP_
#define GNEISS_WORLD_HPP_

#include "engine/core/entity_id.hpp"
#include "engine/values/core/result.hpp"
#include "engine/values/reflection.hpp"
#include "engine/values/render.hpp"
#include "engine/values/scene.hpp"
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

} // namespace gneiss

// 迁移期内部辅助，不随安装包发布。

#endif
