// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/log/log_dispatcher.hpp"
#include "engine/platform/window_configuration.hpp"

#include <functional>
#include <gneiss/engine/application.h>
#include <gneiss/engine/core/result.hpp>
#include <string_view>

namespace gneiss::application_internal {

/** 原生平台和日志回调由配置复制；捕获的借用对象须存活至关闭结束。
 * update/diagnostic/close 与时钟暂保留兼容回调，后续单独迁移。 */
struct application_callbacks final {
  void* user_data = nullptr;
  std::function<result()> initialize;
  std::function<result(bool&)> poll_events;
  gneiss_application_now_ns_fn now_ns = nullptr;
  gneiss_application_update_fn update = nullptr;
  std::function<void()> shutdown;
  gneiss_application_diagnostic_fn diagnostic = nullptr;
  gneiss_application_close_requested_fn close_requested = nullptr;
  log_internal::event_sink log;
};

/** 原生创建配置；字符串仅在 initialize 期间借用，ABI 版本补齐由接口层负责。 */
struct application_configuration final {
  application_callbacks callbacks;
  bool use_granit_window = false;
  platform::window_configuration window;
  std::string_view asset_root;
  std::string_view environment_asset;
  float environment_intensity = 1.0F;
  float environment_rotation_radians = 0.0F;
};

} // namespace gneiss::application_internal
