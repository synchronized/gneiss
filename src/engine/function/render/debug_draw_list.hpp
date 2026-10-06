// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RENDER_DEBUG_DRAW_LIST_HPP_
#define GNEISS_RENDER_DEBUG_DRAW_LIST_HPP_

#include <gneiss/render.h>

#include <span>
#include <vector>

namespace gneiss::render_internal {

class debug_draw_list final {
public:
  /** 同步复制线段；失败保留上一份列表。 */
  [[nodiscard]] gneiss_result replace(std::span<const gneiss_debug_line> lines) noexcept;
  void clear() noexcept { lines_.clear(); }
  [[nodiscard]] const std::vector<gneiss_debug_line>& lines() const noexcept { return lines_; }

private:
  std::vector<gneiss_debug_line> lines_;
};

} // namespace gneiss::render_internal

#endif
