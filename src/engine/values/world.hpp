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
