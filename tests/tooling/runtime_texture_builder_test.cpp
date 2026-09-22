// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "tooling/asset_build/runtime_texture_builder.h"

#include "asset/texture_binary.h"

#include <granit/renderer/texture_asset.hpp>

#include <cstddef>
#include <string>
#include <vector>

int main() { // NOLINT(bugprone-exception-escape)
  gneiss::asset_internal::texture_ktx2 source{
      .transfer = gneiss::asset_internal::texture_transfer::srgb,
      .levels = {{.width = 2U,
                  .height = 2U,
                  .pixels = {std::byte{255U}, std::byte{}, std::byte{}, std::byte{255U},
                             std::byte{}, std::byte{255U}, std::byte{}, std::byte{255U},
                             std::byte{}, std::byte{}, std::byte{255U}, std::byte{255U},
                             std::byte{255U}, std::byte{255U}, std::byte{255U}, std::byte{255U}}},
                 {.width = 1U,
                  .height = 1U,
                  .pixels = {std::byte{128U}, std::byte{128U}, std::byte{128U}, std::byte{255U}}}}};
  std::vector<std::byte> encoded;
  std::string diagnostic;
  if (gneiss::tooling::asset_build::build_runtime_texture(source, encoded, diagnostic) !=
      gneiss::tooling::asset_build::runtime_texture_build_result::success) {
    return 1;
  }
  gneiss::asset_internal::texture_binary_view binary;
  if (gneiss::asset_internal::decode_texture_binary(encoded, binary, diagnostic) !=
      gneiss::asset_internal::texture_binary_result::success) {
    return 2;
  }
  granit::texture_asset_info info;
  if (granit::inspect_texture_asset(binary.manifest, info) != granit::result::success ||
      info.width != 2U || info.height != 2U || info.mip_levels != 2U ||
      info.variants.size() != 2U || info.subresources.size() != 4U ||
      info.variants[0].format != granit::texture_format::bc7_rgba_srgb ||
      info.variants[1].format != granit::texture_format::rgba8_srgb ||
      info.variants[1].payload_offset != 32U || binary.payload.size() != 52U) {
    return 3;
  }
  auto repeated = encoded;
  if (gneiss::tooling::asset_build::build_runtime_texture(source, repeated, diagnostic) !=
          gneiss::tooling::asset_build::runtime_texture_build_result::success ||
      repeated != encoded) {
    return 4;
  }
  source.levels[1].width = 2U;
  return gneiss::tooling::asset_build::build_runtime_texture(source, repeated, diagnostic) ==
                 gneiss::tooling::asset_build::runtime_texture_build_result::invalid_argument
             ? 0
             : 5;
}
