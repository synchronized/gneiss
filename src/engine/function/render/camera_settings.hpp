// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

namespace gneiss::render_internal {
/** 透视投影参数；是否作为活动相机由 World 决定。 */
struct camera_settings {
  float vertical_field_of_view_radians{};
  float near_plane{};
  float far_plane{};
};
} // namespace gneiss::render_internal
