// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/result.hpp"
#include "engine/function/render/texture_view.hpp"

#include <gneiss/engine/render.h>
#include <limits>

namespace gneiss::engine::detail {

/** 先验证字节数可表示为本机范围，再构造借用视图；失败保留输出。 */
[[nodiscard]] inline gneiss::engine::result
read_texture_description(const gneiss_texture_desc& desc,
                         gneiss::engine::texture_view& output) noexcept {
  if (desc.struct_size < sizeof(gneiss_texture_desc) || desc.reserved[0] != 0U ||
      desc.reserved[1] != 0U || desc.pixels == nullptr ||
      desc.pixel_data_size > std::numeric_limits<std::size_t>::max()) {
    return gneiss::engine::result::invalid_argument;
  }
  output = {.width = desc.width,
            .height = desc.height,
            .format = static_cast<gneiss::engine::texture_format>(desc.format),
            .color_space = static_cast<gneiss::engine::texture_color_space>(desc.color_space),
            .row_stride_bytes = desc.row_stride_bytes,
            .pixels = {desc.pixels, static_cast<std::size_t>(desc.pixel_data_size)}};
  return gneiss::engine::result::success;
}

} // namespace gneiss::engine::detail
