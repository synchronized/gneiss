// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

#include <gneiss/engine/core/meta/property.hpp>

namespace gneiss::meta {
/** Transform 的反射属性标识；注册和属性路径共用，不依赖 World。 */
inline constexpr property_id transform_translation_field{1};
inline constexpr property_id transform_rotation_field{2};
inline constexpr property_id transform_scale_field{3};
}
