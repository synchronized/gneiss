// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/math/types.hpp"
#include "engine/core/rid.hpp"
#include "engine/platform/native_window_info.hpp"
#include <vector>
namespace gneiss::render {
struct view {
  rid scene{};
  math::matrix4 view_matrix{1.F};
  math::matrix4 projection_matrix{1.F};
  math::vector3 camera_position{};
  std::uint64_t layers{~UINT64_C(0)};
};
/** 当前只支持一个窗口视图；多视图返回 unsupported，不静默忽略。
 * window 暂沿用窗口输入，待目标资源接口落地后迁移为 target RID。 */
struct frame_request {
  native_window_info window;
  std::vector<view> views;
};
}
