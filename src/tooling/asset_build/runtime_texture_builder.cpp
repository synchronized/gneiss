// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "tooling/asset_build/runtime_texture_builder.h"

#include "tooling/asset_build/bc7_encoder.h"
#include "tooling/asset_build/sha256.h"

#include "asset/texture_binary.h"

#include <granit/renderer/texture_asset.hpp>

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>
#include <span>
#include <utility>

namespace gneiss::tooling::asset_build {
namespace {

constexpr granit_texture_usage texture_usage =
    GRANIT_TEXTURE_USAGE_SAMPLED_BIT | GRANIT_TEXTURE_USAGE_TRANSFER_DESTINATION_BIT;

[[nodiscard]] runtime_texture_build_result
fail(runtime_texture_build_result result, std::string message, std::string& diagnostic) noexcept {
  try {
    diagnostic = std::move(message);
  } catch (...) {
    diagnostic.clear();
    return runtime_texture_build_result::out_of_memory;
  }
  return result;
}

void append_u32(std::vector<std::byte>& bytes, std::uint32_t value) {
  for (std::uint32_t shift = 0U; shift < 32U; shift += 8U) {
    bytes.push_back(static_cast<std::byte>(value >> shift));
  }
}

[[nodiscard]] bool all_zero(const sha256_digest& digest) noexcept {
  return std::ranges::all_of(digest, [](std::byte value) { return value == std::byte{}; });
}

void copy_digest(const sha256_digest& source, std::uint8_t* destination) noexcept {
  std::memcpy(destination, source.data(), source.size());
}

} // namespace

runtime_texture_build_result build_runtime_texture(const asset_internal::texture_ktx2& source,
                                                   std::vector<std::byte>& output,
                                                   std::string& diagnostic) noexcept {
  output.clear();
  diagnostic.clear();
  if (source.levels.empty() || source.levels.size() > std::numeric_limits<std::uint32_t>::max()) {
    return fail(runtime_texture_build_result::invalid_argument, "运行纹理 Mip 链为空或过长",
                diagnostic);
  }
  try {
    std::vector<std::byte> bc7_payload;
    std::vector<std::byte> rgba_payload;
    std::vector<granit_texture_asset_subresource_info> subresources;
    subresources.reserve(source.levels.size() * 2U);

    auto expected_width = source.levels.front().width;
    auto expected_height = source.levels.front().height;
    if (expected_width == 0U || expected_height == 0U) {
      return fail(runtime_texture_build_result::invalid_argument, "运行纹理尺寸为零", diagnostic);
    }
    std::size_t expected_level_count = 1U;
    for (auto width = expected_width, height = expected_height; width != 1U || height != 1U;
         width = std::max(1U, width / 2U), height = std::max(1U, height / 2U)) {
      ++expected_level_count;
    }
    if (source.levels.size() != expected_level_count) {
      return fail(runtime_texture_build_result::invalid_argument, "运行纹理不是完整 Mip 链",
                  diagnostic);
    }
    for (std::uint32_t mip = 0U; mip < source.levels.size(); ++mip) {
      const auto& level = source.levels[mip];
      const auto expected_size = static_cast<std::uint64_t>(level.width) * level.height * 4U;
      if (level.width != expected_width || level.height != expected_height ||
          level.width > std::numeric_limits<std::uint32_t>::max() / 4U ||
          expected_size != level.pixels.size()) {
        return fail(runtime_texture_build_result::invalid_argument, "运行纹理 Mip 布局无效",
                    diagnostic);
      }
      tooling_internal::bc7_image encoded;
      if (tooling_internal::encode_bc7_rgba8(
              level.width, level.height, level.pixels,
              source.transfer == asset_internal::texture_transfer::srgb, encoded,
              diagnostic) != tooling_internal::bc7_encode_result::success) {
        return runtime_texture_build_result::encode_failed;
      }
      const auto offset = bc7_payload.size();
      bc7_payload.insert(bc7_payload.end(), encoded.blocks.begin(), encoded.blocks.end());
      subresources.push_back({.mip_level = mip,
                              .array_layer = 0U,
                              .data_offset = offset,
                              .data_size = encoded.blocks.size(),
                              .bytes_per_row = encoded.block_columns * 16U,
                              .rows_per_image = encoded.block_rows,
                              .reserved = {0U, 0U}});
      expected_width = std::max(1U, expected_width / 2U);
      expected_height = std::max(1U, expected_height / 2U);
    }

    const auto rgba_first_subresource = static_cast<std::uint32_t>(subresources.size());
    for (std::uint32_t mip = 0U; mip < source.levels.size(); ++mip) {
      const auto& level = source.levels[mip];
      const auto offset = rgba_payload.size();
      rgba_payload.insert(rgba_payload.end(), level.pixels.begin(), level.pixels.end());
      subresources.push_back({.mip_level = mip,
                              .array_layer = 0U,
                              .data_offset = offset,
                              .data_size = level.pixels.size(),
                              .bytes_per_row = level.width * 4U,
                              .rows_per_image = level.height,
                              .reserved = {0U, 0U}});
    }

    const auto bc7_digest = sha256(bc7_payload);
    const auto rgba_digest = sha256(rgba_payload);
    std::vector<std::byte> identity;
    identity.reserve(13U + rgba_payload.size());
    append_u32(identity, source.levels.front().width);
    append_u32(identity, source.levels.front().height);
    append_u32(identity, static_cast<std::uint32_t>(source.levels.size()));
    identity.push_back(source.transfer == asset_internal::texture_transfer::srgb ? std::byte{1U}
                                                                                 : std::byte{});
    identity.insert(identity.end(), rgba_payload.begin(), rgba_payload.end());
    const auto content_id = sha256(identity);
    if (all_zero(bc7_digest) || all_zero(rgba_digest) || all_zero(content_id)) {
      return fail(runtime_texture_build_result::out_of_memory, "计算运行纹理摘要失败", diagnostic);
    }

    std::vector<std::byte> payload = bc7_payload;
    const auto rgba_payload_offset = payload.size();
    payload.insert(payload.end(), rgba_payload.begin(), rgba_payload.end());
    granit::texture_asset_info info{.content_id = content_id,
                                    .dimension = GRANIT_TEXTURE_DIMENSION_2D,
                                    .width = source.levels.front().width,
                                    .height = source.levels.front().height,
                                    .depth = 1U,
                                    .array_layers = 1U,
                                    .mip_levels = static_cast<std::uint32_t>(source.levels.size()),
                                    .variants = {},
                                    .subresources = std::move(subresources)};
    info.variants.resize(2U);
    auto& bc7 = info.variants[0];
    bc7.format = source.transfer == asset_internal::texture_transfer::srgb
                     ? GRANIT_TEXTURE_FORMAT_BC7_RGBA_SRGB
                     : GRANIT_TEXTURE_FORMAT_BC7_RGBA_UNORM;
    bc7.usage = texture_usage;
    bc7.first_subresource = 0U;
    bc7.subresource_count = info.mip_levels;
    bc7.payload_offset = 0U;
    bc7.payload_size = bc7_payload.size();
    copy_digest(bc7_digest, bc7.payload_digest);
    auto& rgba = info.variants[1];
    rgba.format = source.transfer == asset_internal::texture_transfer::srgb
                      ? GRANIT_TEXTURE_FORMAT_RGBA8_SRGB
                      : GRANIT_TEXTURE_FORMAT_RGBA8_UNORM;
    rgba.usage = texture_usage;
    rgba.first_subresource = rgba_first_subresource;
    rgba.subresource_count = info.mip_levels;
    rgba.payload_offset = rgba_payload_offset;
    rgba.payload_size = rgba_payload.size();
    copy_digest(rgba_digest, rgba.payload_digest);

    std::vector<std::byte> manifest;
    if (granit::encode_texture_asset(info, manifest) != granit::result::success) {
      return fail(runtime_texture_build_result::encode_failed,
                  "Granit Texture Asset Manifest 编码失败", diagnostic);
    }
    if (asset_internal::encode_texture_binary(manifest, payload, output, diagnostic) !=
        asset_internal::texture_binary_result::success) {
      return runtime_texture_build_result::encode_failed;
    }
    return runtime_texture_build_result::success;
  } catch (const std::bad_alloc&) {
    output.clear();
    return fail(runtime_texture_build_result::out_of_memory, "构建运行纹理内存不足", diagnostic);
  } catch (...) {
    output.clear();
    return fail(runtime_texture_build_result::encode_failed, "构建运行纹理失败", diagnostic);
  }
}

} // namespace gneiss::tooling::asset_build
