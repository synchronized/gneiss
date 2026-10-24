// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/render/debug_draw_list.hpp"
#include "engine/core/result.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <new>

namespace gneiss::engine::render {

gneiss::engine::core::result debug_draw_list::replace(std::span<const debug_line> lines) noexcept {
  constexpr std::uint32_t maximum_lines = 1024U * 1024U;
  if (lines.size() > maximum_lines) {
    return gneiss::engine::core::result::invalid_argument;
  }
  for (const auto& line : lines) {
    if (!std::isfinite(line.width) || line.width <= 0.0F ||
        !std::ranges::all_of(line.start, [](float value) { return std::isfinite(value); }) ||
        !std::ranges::all_of(line.end, [](float value) { return std::isfinite(value); })) {
      return gneiss::engine::core::result::invalid_argument;
    }
  }
  try {
    std::vector<debug_line> pending;
    if (!lines.empty()) {
      pending.assign(lines.begin(), lines.end());
    }
    lines_ = std::move(pending);
    return gneiss::engine::core::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::core::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::core::result::internal;
  }
}

} // namespace gneiss::engine::render
