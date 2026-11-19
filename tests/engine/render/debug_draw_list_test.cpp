// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/core/base/error.hpp>
#include <gneiss/engine/function/render/debug_draw_list.hpp>

#include <limits>

int main() {
  gneiss::debug_draw_list list;
  gneiss::debug_line line{.start = {0.0F, 0.0F, 0.0F},
                                  .end = {1.0F, 0.0F, 0.0F},
                                  .color_rgba8 = UINT32_C(0xffffffff),
                                  .width = 1.0F,
                                  .depth_test = true};
  if (list.replace({&line, 1U}) != gneiss::error::success || list.lines().size() != 1U) {
    return 1;
  }
  line.width = std::numeric_limits<float>::quiet_NaN();
  if (list.replace({&line, 1U}) != gneiss::error::invalid_argument ||
      list.lines().size() != 1U || list.lines()[0].width != 1.0F) {
    return 2;
  }
  if (list.replace({}) != gneiss::error::success || !list.lines().empty()) {
    return 4;
  }
  list.clear();
  return list.lines().empty() ? 0 : 3;
}
