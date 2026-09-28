// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "tooling/asset_build/texture_mip_generator.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace gneiss::tooling::asset_build {
namespace {

double linear_color(double value) {
  return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
}

double srgb_color(double value) {
  return value <= 0.0031308 ? value * 12.92 : (1.055 * std::pow(value, 1.0 / 2.4)) - 0.055;
}

double decode_sample(std::byte input, std::size_t channel, bool srgb, bool normal_map) {
  const auto value = std::to_integer<unsigned>(input) / 255.0;
  if (channel == 3U) {
    return value;
  }
  if (normal_map) {
    return (value * 2.0) - 1.0;
  }
  return srgb ? linear_color(value) : value;
}

std::array<double, 4> filter_pixel(const asset_internal::texture_mip& source, std::uint32_t x,
                                   std::uint32_t y, std::uint32_t width, std::uint32_t height,
                                   bool srgb, bool normal_map) {
  const double left = static_cast<double>(x) * source.width / width;
  const double right = static_cast<double>(x + 1U) * source.width / width;
  const double top = static_cast<double>(y) * source.height / height;
  const double bottom = static_cast<double>(y + 1U) * source.height / height;
  std::array<double, 4> value{};
  double total{};
  for (auto source_y = static_cast<std::uint32_t>(top);
       source_y < static_cast<std::uint32_t>(std::ceil(bottom)); ++source_y) {
    for (auto source_x = static_cast<std::uint32_t>(left);
         source_x < static_cast<std::uint32_t>(std::ceil(right)); ++source_x) {
      const auto weight = (std::min(right, static_cast<double>(source_x + 1U)) -
                           std::max(left, static_cast<double>(source_x))) *
                          (std::min(bottom, static_cast<double>(source_y + 1U)) -
                           std::max(top, static_cast<double>(source_y)));
      const auto offset = ((static_cast<std::size_t>(source_y) * source.width) + source_x) * 4U;
      for (std::size_t channel = 0; channel < 4U; ++channel) {
        value[channel] +=
            decode_sample(source.pixels[offset + channel], channel, srgb, normal_map) * weight;
      }
      total += weight;
    }
  }
  for (auto& component : value) {
    component /= total;
  }
  if (normal_map) {
    const auto length =
        std::sqrt((value[0] * value[0]) + (value[1] * value[1]) + (value[2] * value[2]));
    if (length <= 1.0e-8) {
      value = {0.0, 0.0, 1.0, value[3]};
    } else {
      for (std::size_t channel = 0; channel < 3U; ++channel) {
        value[channel] /= length;
      }
    }
    for (std::size_t channel = 0; channel < 3U; ++channel) {
      value[channel] = (value[channel] + 1.0) * 0.5;
    }
  } else if (srgb) {
    for (std::size_t channel = 0; channel < 3U; ++channel) {
      value[channel] = srgb_color(value[channel]);
    }
  }
  return value;
}

} // namespace

std::vector<std::byte> generate_texture_mip(const asset_internal::texture_mip& source,
                                            asset_internal::texture_transfer transfer,
                                            bool normal_map) {
  if (source.width == 0U || source.height == 0U || source.width > 16384U ||
      source.height > 16384U ||
      source.pixels.size() != static_cast<std::uint64_t>(source.width) * source.height * 4U ||
      (normal_map && transfer != asset_internal::texture_transfer::linear)) {
    return {};
  }
  const auto width = std::max(1U, source.width / 2U);
  const auto height = std::max(1U, source.height / 2U);
  std::vector<std::byte> output(static_cast<std::size_t>(width) * height * 4U);
  for (std::uint32_t y = 0; y < height; ++y) {
    for (std::uint32_t x = 0; x < width; ++x) {
      const auto pixel =
          filter_pixel(source, x, y, width, height,
                       transfer == asset_internal::texture_transfer::srgb, normal_map);
      const auto offset = ((static_cast<std::size_t>(y) * width) + x) * 4U;
      for (std::size_t channel = 0; channel < 4U; ++channel) {
        output[offset + channel] = static_cast<std::byte>(
            static_cast<unsigned>(std::lround(std::clamp(pixel[channel], 0.0, 1.0) * 255.0)));
      }
    }
  }
  return output;
}

} // namespace gneiss::tooling::asset_build
