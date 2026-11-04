// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/math/transform.hpp"
#include "engine/core/meta/property.hpp"
namespace gneiss::engine {
/** 内建组件的持久化身份；不依赖 Registry、World 或 C ABI。 */
inline constexpr guid transform_component_type = math::transform::type_guid;
inline constexpr guid camera_component_type{
    guid::from_bytes({0xd1, 0x3a, 0x72, 0xf5, 0x71, 0xed, 0x43, 0xd6, 0x98, 0x98, 0x12, 0x06, 0x3f,
                      0xd3, 0x71, 0x83})};
inline constexpr meta::property_id transform_translation_field{1};
inline constexpr meta::property_id transform_rotation_field{2};
inline constexpr meta::property_id transform_scale_field{3};
inline constexpr meta::property_id camera_vertical_field_of_view_field{1};
inline constexpr meta::property_id camera_near_plane_field{2};
inline constexpr meta::property_id camera_far_plane_field{3};
inline constexpr meta::property_id camera_is_primary_field{4};
}
