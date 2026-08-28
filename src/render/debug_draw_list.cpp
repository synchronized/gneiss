// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "render/debug_draw_list.h"

#include <algorithm>
#include <cmath>
#include <new>

namespace gneiss::render_internal {

gneiss_result debug_draw_list::replace(const gneiss_debug_draw_list_desc& desc) noexcept {
  constexpr std::uint32_t maximum_lines = 1024U * 1024U;
  if (desc.struct_size < GNEISS_DEBUG_DRAW_LIST_DESC_VERSION_1_SIZE || desc.reserved != 0U ||
      desc.reserved_2 != 0U || desc.line_count > maximum_lines ||
      (desc.line_count != 0U && desc.lines == nullptr)) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  for (std::uint32_t index = 0; index < desc.line_count; ++index) {
    const auto& line = desc.lines[index];
    if (line.depth_test > 1U || line.reserved[0] != 0U || line.reserved[1] != 0U ||
        line.reserved[2] != 0U || !std::isfinite(line.width) || line.width <= 0.0F ||
        !std::ranges::all_of(line.start, [](float value) { return std::isfinite(value); }) ||
        !std::ranges::all_of(line.end, [](float value) { return std::isfinite(value); })) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
  }
  try {
    std::vector<gneiss_debug_line> pending;
    if (desc.line_count != 0U) {
      pending.assign(desc.lines, desc.lines + desc.line_count);
    }
    lines_ = std::move(pending);
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

} // namespace gneiss::render_internal
