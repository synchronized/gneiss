// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RENDER_DEBUG_DRAW_LIST_HPP_
#define GNEISS_RENDER_DEBUG_DRAW_LIST_HPP_

#include "engine/core/error.hpp"
#include "engine/function/render/draw_types.hpp"

#include <span>
#include <vector>

namespace gneiss::engine {

class debug_draw_list final {
public:
  /** 同步复制线段；失败保留上一份列表。 */
  [[nodiscard]] error replace(std::span<const debug_line> lines) noexcept;
  void clear() noexcept { lines_.clear(); }
  [[nodiscard]] const std::vector<debug_line>& lines() const noexcept { return lines_; }

private:
  std::vector<debug_line> lines_;
};

} // namespace gneiss::engine

#endif
