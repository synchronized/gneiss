// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <gneiss/render.h>

#include <cstdint>
#include <span>

namespace gneiss::render_internal {

/** 单层原始纹理的同步借用视图；创建结束后不保留源像素。 */
struct texture_view {
  std::uint32_t width{};
  std::uint32_t height{};
  gneiss_texture_format format{GNEISS_TEXTURE_FORMAT_RGBA8_UNORM};
  gneiss_texture_color_space color_space{GNEISS_TEXTURE_COLOR_SPACE_SRGB};
  std::uint32_t row_stride_bytes{};
  std::span<const std::uint8_t> pixels;
};

} // namespace gneiss::render_internal
