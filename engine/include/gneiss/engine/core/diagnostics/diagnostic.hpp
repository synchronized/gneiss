// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <gneiss/engine/error.hpp>

#include <cstdint>
#include <string_view>

namespace gneiss {
enum class severity : std::uint32_t { info = 1, warning, error };
enum class category : std::uint32_t { application = 1, asset, input, backend };
/** 文本仅在接收回调期间有效；接收方不得保存借用视图。 */
struct diagnostic_view {
  severity level{severity::error};
  category source{category::application};
  gneiss::error result{gneiss::error::success};
  std::string_view module;
  std::string_view message;
};
} // namespace gneiss
