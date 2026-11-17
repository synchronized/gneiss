// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <gneiss/engine/core/math/types.hpp>
#include <gneiss/engine/core/types/rid.hpp>
#include <gneiss/engine/function/render/debug_draw_list.hpp>
#include <gneiss/engine/function/render/ui_draw_list.hpp>
#include <gneiss/engine/platform/window/native_window_info.hpp>
#include <vector>
namespace gneiss::render {
/** 固定尺寸 RGBA8 离屏输出；改变尺寸通过创建新目标替换，旧帧继续使用旧目标。 */
struct target_description {
  std::uint32_t width{};
  std::uint32_t height{};
};
struct view {
  /** 零表示空输出，可用于只合成窗口 UI。 */
  rid scene{};
  /** 零表示当前窗口；非零必须是本服务创建的离屏目标。 */
  rid target{};
  math::matrix4 view_matrix{1.F};
  math::matrix4 projection_matrix{1.F};
  math::vector3 camera_position{};
  std::uint64_t layers{~UINT64_C(0)};
};
/** 支持多个独立目标视图，目标不得重复（窗口最多一个）；零视图绘制窗口 UI/调试输入。
 * 离屏视图使用目标尺寸，window 只为窗口输出提供输入。UI 只应用于窗口输出，调试输入应用于每个视图。
 */
struct frame_request {
  gneiss::window::native_window_info window;
  std::vector<view> views;
  ui_draw_list ui{};
  debug_draw_list debug{};
};
}
