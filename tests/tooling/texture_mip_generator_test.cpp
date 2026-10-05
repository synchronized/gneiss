// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "tooling/asset_build/texture_mip_generator.hpp"

#include <array>

int main() {
  using gneiss::asset_internal::texture_mip;
  using gneiss::asset_internal::texture_transfer;
  using gneiss::tooling::asset_build::generate_texture_mip;
  texture_mip source{.width = 2U,
                     .height = 1U,
                     .pixels = {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0},
                                std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255}}};
  const auto srgb = generate_texture_mip(source, texture_transfer::srgb, false);
  const auto linear = generate_texture_mip(source, texture_transfer::linear, false);
  if (srgb.size() != 4U || srgb[0] != std::byte{188} || srgb[3] != std::byte{128} ||
      linear[0] != std::byte{128} || linear[3] != std::byte{128})
    return 1;
  source.width = 3U;
  source.pixels.insert(source.pixels.begin(), 4U, std::byte{0});
  const auto odd = generate_texture_mip(source, texture_transfer::linear, false);
  if (odd[0] != std::byte{85})
    return 2;
  source.width = 2U;
  source.pixels = {std::byte{255}, std::byte{128}, std::byte{128}, std::byte{255},
                   std::byte{0},   std::byte{127}, std::byte{127}, std::byte{255}};
  const auto normal = generate_texture_mip(source, texture_transfer::linear, true);
  if (normal[0] != std::byte{128} || normal[1] != std::byte{128} || normal[2] != std::byte{255})
    return 3;
  if (!generate_texture_mip(source, texture_transfer::srgb, true).empty())
    return 4;
  source.pixels.pop_back();
  if (!generate_texture_mip(source, texture_transfer::linear, true).empty())
    return 5;
  source.width = 0U;
  if (!generate_texture_mip(source, texture_transfer::linear, false).empty())
    return 6;
  return 0;
}
