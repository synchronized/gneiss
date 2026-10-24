// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/log/log_dispatcher.hpp"
#include "engine/platform/window_configuration.hpp"

#include "engine/core/diagnostics/diagnostic.hpp"
#include "engine/core/frame_time.hpp"
#include "engine/core/result.hpp"
#include "engine/core/rid.hpp"
#include <functional>
#include <string_view>

namespace gneiss::runtime {

/** 原生平台和日志回调由配置复制；捕获的借用对象须存活至关闭结束。
 * 宿主通过捕获绑定需要的状态，不传递 Application 句柄。 */
struct application_callbacks final {
  std::function<gneiss::engine::result()> initialize;
  std::function<gneiss::engine::result(bool&)> poll_events;
  std::function<std::uint64_t()> now_ns;
  std::function<gneiss::engine::result(const gneiss::engine::frame_time&)> update;
  std::function<void()> shutdown;
  std::function<void(const gneiss::engine::diagnostic_view&)> diagnostic;
  std::function<bool()> close_requested;
  gneiss::engine::event_sink log;
};

/** 原生创建配置；字符串仅在 initialize 期间借用。 */
struct application_configuration final {
  application_callbacks callbacks;
  bool use_granit_window = false;
  gneiss::engine::window_configuration window;
  std::string_view asset_root;
  std::string_view environment_asset;
  float environment_intensity = 1.0F;
  float environment_rotation_radians = 0.0F;
};

} // namespace gneiss::runtime
