// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/resource/texture/texture_description.hpp"

#include <cstdint>
#include <span>

namespace gneiss::engine {

/** 单层原始纹理的同步借用视图；创建结束后不保留源像素。 */
struct texture_view {
  std::uint32_t width{};
  std::uint32_t height{};
  texture_format format{texture_format::rgba8_unorm};
  texture_color_space color_space{texture_color_space::srgb};
  std::uint32_t row_stride_bytes{};
  std::span<const std::uint8_t> pixels;
};

} // namespace gneiss::engine
