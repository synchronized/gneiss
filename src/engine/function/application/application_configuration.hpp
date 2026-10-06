// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/platform/window_configuration.hpp"

#include <gneiss/engine/application.h>
#include <string_view>

namespace gneiss::application_internal {

/** 真正的用户回调 ABI 边界；函数与上下文由宿主持有至关闭结束，不复制宿主状态。 */
struct application_callbacks final {
  void* user_data = nullptr;
  gneiss_application_initialize_fn initialize = nullptr;
  gneiss_application_poll_events_fn poll_events = nullptr;
  gneiss_application_now_ns_fn now_ns = nullptr;
  gneiss_application_update_fn update = nullptr;
  gneiss_application_shutdown_fn shutdown = nullptr;
  gneiss_application_diagnostic_fn diagnostic = nullptr;
  gneiss_application_close_requested_fn close_requested = nullptr;
  gneiss_application_log_fn log = nullptr;
};

/** ABI 入口完成版本补齐和校验后的配置；字符串仅在 initialize 期间使用。 */
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
