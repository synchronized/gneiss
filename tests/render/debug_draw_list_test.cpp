// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/api/draw_description.hpp"
#include "engine/core/result.hpp"
#include "engine/function/render/debug_draw_list.hpp"

#include <limits>

int main() {
  gneiss::engine::debug_draw_list list;
  gneiss::engine::debug_line line{.start = {0.0F, 0.0F, 0.0F},
                                  .end = {1.0F, 0.0F, 0.0F},
                                  .color_rgba8 = UINT32_C(0xffffffff),
                                  .width = 1.0F,
                                  .depth_test = true};
  if (list.replace({&line, 1U}) != gneiss::engine::result::success || list.lines().size() != 1U) {
    return 1;
  }
  line.width = std::numeric_limits<float>::quiet_NaN();
  if (list.replace({&line, 1U}) != gneiss::engine::result::invalid_argument ||
      list.lines().size() != 1U || list.lines()[0].width != 1.0F) {
    return 2;
  }
  if (list.replace({}) != gneiss::engine::result::success || !list.lines().empty()) {
    return 4;
  }
  gneiss_debug_line abi_line{};
  abi_line.width = 2.0F;
  abi_line.depth_test = 1;
  gneiss_debug_draw_list_desc desc = GNEISS_DEBUG_DRAW_LIST_DESC_INIT;
  desc.lines = &abi_line;
  desc.line_count = 1;
  std::vector<gneiss::engine::debug_line> converted;
  if (gneiss::engine::detail::read_debug_draw_description(desc, converted) !=
          gneiss::engine::result::success ||
      converted.size() != 1 || !converted[0].depth_test || converted[0].width != 2.0F)
    return 5;
  abi_line.reserved[0] = 1;
  if (gneiss::engine::detail::read_debug_draw_description(desc, converted) !=
          gneiss::engine::result::invalid_argument ||
      converted[0].width != 2.0F)
    return 6;
  abi_line.reserved[0] = 0;
  abi_line.depth_test = 2;
  if (gneiss::engine::detail::read_debug_draw_description(desc, converted) !=
      gneiss::engine::result::invalid_argument)
    return 7;
  list.clear();
  return list.lines().empty() ? 0 : 3;
}
