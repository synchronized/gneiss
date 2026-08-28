// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "render/debug_draw_list.h"

#include <limits>

int main() {
  gneiss::render_internal::debug_draw_list list;
  gneiss_debug_line line{.start = {0.0F, 0.0F, 0.0F},
                         .end = {1.0F, 0.0F, 0.0F},
                         .color_rgba8 = UINT32_C(0xffffffff),
                         .width = 1.0F,
                         .depth_test = 1U,
                         .reserved = {}};
  gneiss_debug_draw_list_desc desc = GNEISS_DEBUG_DRAW_LIST_DESC_INIT;
  desc.line_count = 1U;
  desc.lines = &line;
  if (list.replace(desc) != GNEISS_SUCCESS || list.lines().size() != 1U) {
    return 1;
  }
  line.width = std::numeric_limits<float>::quiet_NaN();
  if (list.replace(desc) != GNEISS_ERROR_INVALID_ARGUMENT || list.lines().size() != 1U) {
    return 2;
  }
  list.clear();
  return list.lines().empty() ? 0 : 3;
}
