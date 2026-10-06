// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "tooling/asset_build/bc7_encoder.hpp"

#include <bc7decomp.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

int main() { // NOLINT(bugprone-exception-escape)
  using namespace gneiss::tooling_internal;
  constexpr std::uint32_t width = 5U;
  constexpr std::uint32_t height = 3U;
  std::vector<std::byte> source(width * height * 4U);
  for (std::uint32_t y = 0U; y < height; ++y) {
    for (std::uint32_t x = 0U; x < width; ++x) {
      const auto offset = (y * width + x) * 4U;
      source[offset] = static_cast<std::byte>(30U + x * 35U);
      source[offset + 1U] = static_cast<std::byte>(20U + y * 70U);
      source[offset + 2U] = static_cast<std::byte>(80U + x * 10U + y * 8U);
      source[offset + 3U] = static_cast<std::byte>(40U + x * 30U + y * 20U);
    }
  }

  bc7_image first;
  bc7_image second;
  std::string diagnostic;
  if (encode_bc7_rgba8(width, height, source, true, first, diagnostic) !=
          bc7_encode_result::success ||
      encode_bc7_rgba8(width, height, source, true, second, diagnostic) !=
          bc7_encode_result::success ||
      first.blocks != second.blocks || first.block_columns != 2U || first.block_rows != 1U ||
      first.blocks.size() != 32U) {
    return 1;
  }

  std::uint64_t squared_error{};
  std::uint32_t maximum_alpha_error{};
  for (std::uint32_t block_x = 0U; block_x < first.block_columns; ++block_x) {
    std::array<bc7decomp::color_rgba, 16U> decoded{};
    if (!bc7decomp::unpack_bc7(first.blocks.data() + block_x * 16U, decoded.data())) {
      return 2;
    }
    for (std::uint32_t local_y = 0U; local_y < 4U; ++local_y) {
      for (std::uint32_t local_x = 0U; local_x < 4U; ++local_x) {
        const auto x = std::min(block_x * 4U + local_x, width - 1U);
        const auto y = std::min(local_y, height - 1U);
        const auto source_offset = (y * width + x) * 4U;
        const auto& pixel = decoded[local_y * 4U + local_x];
        for (std::size_t channel = 0U; channel < 4U; ++channel) {
          const auto original = std::to_integer<int>(source[source_offset + channel]);
          const auto difference = original - pixel.m_comps[channel];
          squared_error += static_cast<std::uint64_t>(difference * difference);
        }
        const auto alpha = std::to_integer<int>(source[source_offset + 3U]);
        maximum_alpha_error =
            std::max(maximum_alpha_error, static_cast<std::uint32_t>(std::abs(alpha - pixel.a)));
      }
    }
  }
  if (squared_error / (first.block_columns * 16U * 4U) > 900U || maximum_alpha_error > 32U) {
    return 3;
  }

  if (encode_bc7_rgba8(0U, height, source, false, first, diagnostic) !=
          bc7_encode_result::invalid_argument ||
      diagnostic.empty()) {
    return 4;
  }
  return 0;
}
