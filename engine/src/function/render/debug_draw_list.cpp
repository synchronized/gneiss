// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/function/render/debug_draw_list.hpp>
#include <gneiss/engine/core/base/error.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <new>

namespace gneiss {

error debug_draw_list::replace(std::span<const debug_line> lines) {
  constexpr std::uint32_t maximum_lines = 1024U * 1024U;
  if (lines.size() > maximum_lines) {
    return error::invalid_argument;
  }
  for (const auto& line : lines) {
    if (!std::isfinite(line.width) || line.width <= 0.0F ||
        !std::ranges::all_of(math::components(line.start),
                             [](float value) { return std::isfinite(value); }) ||
        !std::ranges::all_of(math::components(line.end),
                             [](float value) { return std::isfinite(value); })) {
      return error::invalid_argument;
    }
  }
  {
    std::vector<debug_line> pending;
    if (!lines.empty()) {
      pending.assign(lines.begin(), lines.end());
    }
    lines_ = std::move(pending);
    return error::success;
  }
}

} // namespace gneiss
