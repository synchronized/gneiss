// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <cstdint>
#include <string_view>

namespace gneiss::platform {

/** 已校验的窗口配置；title 仅在初始化调用期间借用，不包含 Application 生命周期或 ABI 字段。 */
struct window_configuration final {
  std::string_view title = "Gneiss";
  std::uint32_t width = 1280U;
  std::uint32_t height = 720U;
  bool visible = true;
  bool resizable = true;
  bool high_dpi = true;
};

} // namespace gneiss::platform
