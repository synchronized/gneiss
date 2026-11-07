// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/math/transform.hpp"
#include "engine/core/meta/property.hpp"
namespace gneiss::engine {
/** 内建组件的持久化身份；不依赖 Registry、World 或 C ABI。 */
inline constexpr guid transform_component_type = math::transform::type_guid;
inline constexpr meta::property_id transform_translation_field{1};
inline constexpr meta::property_id transform_rotation_field{2};
inline constexpr meta::property_id transform_scale_field{3};
}
