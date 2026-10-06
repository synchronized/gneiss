// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_TOOLING_ASSET_BUILD_BC7_ENCODER_H_
#define GNEISS_TOOLING_ASSET_BUILD_BC7_ENCODER_H_

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace gneiss::tooling_internal {

struct bc7_image final {
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint32_t block_columns{};
  std::uint32_t block_rows{};
  std::vector<std::byte> blocks;
};

enum class bc7_encode_result : std::uint8_t { success, invalid_argument, out_of_memory };

/** 把紧密 RGBA8 图像确定性编码为 BC7；不足 4×4 的边缘复制最外侧像素。 */
[[nodiscard]] bc7_encode_result encode_bc7_rgba8(std::uint32_t width, std::uint32_t height,
                                                 std::span<const std::byte> rgba, bool perceptual,
                                                 bc7_image& output,
                                                 std::string& diagnostic) noexcept;

} // namespace gneiss::tooling_internal

#endif
