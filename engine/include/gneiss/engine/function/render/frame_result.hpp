// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <gneiss/engine/core/base/error.hpp>
#include <gneiss/engine/function/render/statistics.hpp>
#include <vector>
namespace gneiss::render {
enum class frame_policy : std::uint8_t { replaceable, required };

struct frame_completion final {
  std::uint64_t sequence{};
  error status{error::unknown};
  execution_result execution;
  bool dropped{};
  frame_policy policy{frame_policy::replaceable};
};

/** 截图结果：紧密 RGBA8；由独立请求完成后移交给调用方。 */
struct frame_image final {
  std::uint32_t width{};
  std::uint32_t height{};
  std::vector<std::byte> pixels;
};

} // namespace gneiss::render
