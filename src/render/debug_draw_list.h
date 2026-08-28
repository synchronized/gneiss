// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RENDER_DEBUG_DRAW_LIST_H_
#define GNEISS_RENDER_DEBUG_DRAW_LIST_H_

#include <gneiss/render.h>

#include <vector>

namespace gneiss::render_internal {

class debug_draw_list final {
public:
  [[nodiscard]] gneiss_result replace(const gneiss_debug_draw_list_desc& desc) noexcept;
  void clear() noexcept { lines_.clear(); }
  [[nodiscard]] const std::vector<gneiss_debug_line>& lines() const noexcept { return lines_; }

private:
  std::vector<gneiss_debug_line> lines_;
};

} // namespace gneiss::render_internal

#endif
