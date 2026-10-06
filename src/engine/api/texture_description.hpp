// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/function/render/texture_view.hpp"

#include <limits>

namespace gneiss::api {

/** 先验证字节数可表示为本机范围，再构造借用视图；失败保留输出。 */
[[nodiscard]] inline gneiss_result
read_texture_description(const gneiss_texture_desc& desc,
                         render_internal::texture_view& output) noexcept {
  if (desc.struct_size < sizeof(gneiss_texture_desc) || desc.reserved[0] != 0U ||
      desc.reserved[1] != 0U || desc.pixels == nullptr ||
      desc.pixel_data_size > std::numeric_limits<std::size_t>::max()) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  output = {.width = desc.width,
            .height = desc.height,
            .format = desc.format,
            .color_space = desc.color_space,
            .row_stride_bytes = desc.row_stride_bytes,
            .pixels = {desc.pixels, static_cast<std::size_t>(desc.pixel_data_size)}};
  return GNEISS_SUCCESS;
}

} // namespace gneiss::api
