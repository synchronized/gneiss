// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "tooling/asset_build/runtime_texture_probe.hpp"

#include "engine/asset/texture_binary.hpp"
#include "tooling/asset_build/sha256.hpp"

#include <granit/renderer/texture_asset.hpp>

#include <algorithm>
#include <bit>
#include <sstream>
#include <string_view>
#include <utility>

namespace gneiss::tooling::asset_build {
namespace {

std::string_view format_name(granit::texture_format format) noexcept {
  switch (format) {
  case granit::texture_format::bc7_rgba_unorm:
    return "BC7_UNORM";
  case granit::texture_format::bc7_rgba_srgb:
    return "BC7_SRGB";
  case granit::texture_format::rgba8_unorm:
    return "RGBA8_UNORM";
  case granit::texture_format::rgba8_srgb:
    return "RGBA8_SRGB";
  default:
    return "unsupported";
  }
}

} // namespace

bool inspect_runtime_texture(std::span<const std::byte> bytes, std::string& summary,
                             std::string& diagnostic) noexcept {
  summary.clear();
  diagnostic.clear();
  try {
    asset_internal::texture_binary_view binary;
    if (asset_internal::decode_texture_binary(bytes, binary, diagnostic) !=
        asset_internal::texture_binary_result::success) {
      return false;
    }
    granit::texture_asset_info info;
    if (granit::inspect_texture_asset(binary.manifest, info).failed() ||
        info.dimension != granit::texture_dimension::two_dimensional || info.depth != 1U ||
        info.array_layers != 1U || info.variants.empty() ||
        std::cmp_not_equal(info.mip_levels, std::bit_width(std::max(info.width, info.height)))) {
      diagnostic = "Manifest 无效、不是二维单层纹理或 Mip 链不完整";
      return false;
    }
    const auto first_format = info.variants.front().format;
    const bool srgb = first_format == granit::texture_format::bc7_rgba_srgb ||
                      first_format == granit::texture_format::rgba8_srgb;
    const auto fallback =
        srgb ? granit::texture_format::rgba8_srgb : granit::texture_format::rgba8_unorm;
    if (!std::ranges::any_of(info.variants, [fallback](const auto& variant) {
          return variant.format == fallback;
        })) {
      diagnostic = "缺少 RGBA8 回退变体";
      return false;
    }
    std::ostringstream output;
    output << "Texture Binary v1 " << info.width << 'x' << info.height << " Mip=" << info.mip_levels
           << " Variants=" << info.variants.size() << " ManifestBytes=" << binary.manifest.size()
           << " PayloadBytes=" << binary.payload.size() << '\n';
    for (std::size_t index = 0; index < info.variants.size(); ++index) {
      const auto& variant = info.variants[index];
      const auto preferred =
          srgb ? granit::texture_format::bc7_rgba_srgb : granit::texture_format::bc7_rgba_unorm;
      if ((variant.format != fallback && variant.format != preferred) ||
          variant.payload_offset > binary.payload.size() ||
          variant.payload_size > binary.payload.size() - variant.payload_offset) {
        diagnostic = "变体格式、颜色空间或负载边界无效";
        return false;
      }
      const auto payload = binary.payload.subspan(static_cast<std::size_t>(variant.payload_offset),
                                                  static_cast<std::size_t>(variant.payload_size));
      if (sha256(payload) != variant.payload_digest) {
        diagnostic = "变体 " + std::to_string(index) + " 负载 SHA-256 不匹配";
        return false;
      }
      output << "Variant=" << index << " Format=" << format_name(variant.format)
             << " Bytes=" << variant.payload_size << " SHA256=ok\n";
      for (std::uint32_t sub = 0; sub < variant.subresource_count; ++sub) {
        const auto& mip = info.subresources.at(variant.first_subresource + sub);
        output << "  Mip=" << mip.mip_level << " Offset=" << mip.data_offset
               << " Bytes=" << mip.data_size << " RowBytes=" << mip.bytes_per_row
               << " Rows=" << mip.rows_per_image << '\n';
      }
    }
    summary = output.str();
    return true;
  } catch (...) {
    summary.clear();
    return false;
  }
}

} // namespace gneiss::tooling::asset_build
