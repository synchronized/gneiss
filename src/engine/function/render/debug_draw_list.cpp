// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/render/debug_draw_list.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <new>

namespace gneiss::render_internal {

gneiss_result debug_draw_list::replace(std::span<const gneiss_debug_line> lines) noexcept {
  constexpr std::uint32_t maximum_lines = 1024U * 1024U;
  if (lines.size() > maximum_lines) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  for (const auto& line : lines) {
    if (line.depth_test > 1U || line.reserved[0] != 0U || line.reserved[1] != 0U ||
        line.reserved[2] != 0U || !std::isfinite(line.width) || line.width <= 0.0F ||
        !std::ranges::all_of(line.start, [](float value) { return std::isfinite(value); }) ||
        !std::ranges::all_of(line.end, [](float value) { return std::isfinite(value); })) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
  }
  try {
    std::vector<gneiss_debug_line> pending;
    if (!lines.empty()) {
      pending.assign(lines.begin(), lines.end());
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
