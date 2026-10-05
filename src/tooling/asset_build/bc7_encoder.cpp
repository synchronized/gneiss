// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "tooling/asset_build/bc7_encoder.hpp"

#include <bc7enc.h>

#include <algorithm>
#include <array>
#include <limits>
#include <mutex>
#include <new>
#include <utility>

namespace gneiss::tooling_internal {
namespace {

constexpr std::uint64_t rgba_stride = 4U;
constexpr std::uint64_t bytes_per_block = 16U;

[[nodiscard]] bc7_encode_result fail(bc7_encode_result result, std::string message,
                                     std::string& diagnostic) noexcept {
  try {
    diagnostic = std::move(message);
  } catch (...) {
    diagnostic.clear();
    return bc7_encode_result::out_of_memory;
  }
  return result;
}

} // namespace

bc7_encode_result encode_bc7_rgba8(std::uint32_t width, std::uint32_t height,
                                   std::span<const std::byte> rgba, bool perceptual,
                                   bc7_image& output, std::string& diagnostic) noexcept {
  output = {};
  diagnostic.clear();
  const auto pixel_count = static_cast<std::uint64_t>(width) * height;
  if (width == 0U || height == 0U ||
      pixel_count > std::numeric_limits<std::uint64_t>::max() / rgba_stride ||
      pixel_count * rgba_stride != rgba.size()) {
    return fail(bc7_encode_result::invalid_argument, "BC7 输入尺寸或 RGBA8 字节数无效", diagnostic);
  }

  const auto columns = (static_cast<std::uint64_t>(width) + 3U) / 4U;
  const auto rows = (static_cast<std::uint64_t>(height) + 3U) / 4U;
  if (columns * rows > std::numeric_limits<std::size_t>::max() / bytes_per_block) {
    return fail(bc7_encode_result::invalid_argument, "BC7 输出尺寸溢出", diagnostic);
  }

  try {
    static std::once_flag initialize_flag;
    std::call_once(initialize_flag, bc7enc_compress_block_init);
    bc7enc_compress_block_params parameters{};
    bc7enc_compress_block_params_init(&parameters);
    if (!perceptual) {
      bc7enc_compress_block_params_init_linear_weights(&parameters);
    }
    parameters.m_uber_level = 1U;

    output.width = width;
    output.height = height;
    output.block_columns = static_cast<std::uint32_t>(columns);
    output.block_rows = static_cast<std::uint32_t>(rows);
    output.blocks.resize(static_cast<std::size_t>(columns * rows * bytes_per_block));
    std::array<color_rgba, 16U> pixels{};
    for (std::uint32_t block_y = 0U; block_y < output.block_rows; ++block_y) {
      for (std::uint32_t block_x = 0U; block_x < output.block_columns; ++block_x) {
        for (std::uint32_t local_y = 0U; local_y < 4U; ++local_y) {
          for (std::uint32_t local_x = 0U; local_x < 4U; ++local_x) {
            const auto source_x = std::min((block_x * 4U) + local_x, width - 1U);
            const auto source_y = std::min((block_y * 4U) + local_y, height - 1U);
            const auto source = (static_cast<std::size_t>(source_y) * width + source_x) * 4U;
            auto& pixel = pixels[(local_y * 4U) + local_x];
            for (std::size_t channel = 0U; channel < 4U; ++channel) {
              pixel.m_c[channel] = std::to_integer<std::uint8_t>(rgba[source + channel]);
            }
          }
        }
        const auto destination =
            (static_cast<std::size_t>(block_y) * output.block_columns + block_x) * 16U;
        (void)bc7enc_compress_block(output.blocks.data() + destination, pixels.data(), &parameters);
      }
    }
    return bc7_encode_result::success;
  } catch (const std::bad_alloc&) {
    output = {};
    return fail(bc7_encode_result::out_of_memory, "BC7 编码内存不足", diagnostic);
  } catch (...) {
    output = {};
    return fail(bc7_encode_result::invalid_argument, "BC7 编码失败", diagnostic);
  }
}

} // namespace gneiss::tooling_internal
