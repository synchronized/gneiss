// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/error.hpp"
#include "engine/function/render/render_statistics.hpp"
#include <vector>
namespace gneiss {
enum class render_frame_policy : std::uint8_t { replaceable, required };

struct render_frame_completion final {
  std::uint64_t sequence{};
  error status{error::unknown};
  render_execution_result execution;
  bool dropped{};
  render_frame_policy policy{render_frame_policy::replaceable};
};

/** 内部诊断回读：紧密 RGBA8，所有权属于请求；仅完成回执后可读取。 */
struct frame_image final {
  std::uint32_t width{};
  std::uint32_t height{};
  std::vector<std::byte> pixels;
};

} // namespace gneiss
