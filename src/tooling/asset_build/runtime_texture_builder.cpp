// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "tooling/asset_build/runtime_texture_builder.h"

#include "tooling/asset_build/bc7_encoder.h"

#include "engine/asset/texture_binary.hpp"

#include <granit/asset_tools/texture_builder.hpp>

#include <array>

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>
#include <span>
#include <string_view>
#include <utility>

namespace gneiss::tooling::asset_build {
namespace {

constexpr granit::texture_usage texture_usage =
    granit::texture_usage::sampled | granit::texture_usage::transfer_destination;

[[nodiscard]] runtime_texture_build_result fail(runtime_texture_build_result result,
                                                std::string_view message,
                                                std::string& diagnostic) noexcept {
  try {
    diagnostic.assign(message);
  } catch (...) {
    diagnostic.clear();
    return runtime_texture_build_result::out_of_memory;
  }
  return result;
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
    std::vector<granit::asset_tools::texture::subresource_info> subresources;
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
                              .rows_per_image = encoded.block_rows});
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
                              .rows_per_image = level.height});
    }

    const auto layouts = std::span{subresources};
    const std::array variants{
        granit::asset_tools::texture::variant_desc{
            .format = source.transfer == asset_internal::texture_transfer::srgb
                          ? granit::texture_format::bc7_rgba_srgb
                          : granit::texture_format::bc7_rgba_unorm,
            .usage = texture_usage,
            .payload = bc7_payload,
            .subresources = layouts.first(rgba_first_subresource),
        },
        granit::asset_tools::texture::variant_desc{
            .format = source.transfer == asset_internal::texture_transfer::srgb
                          ? granit::texture_format::rgba8_srgb
                          : granit::texture_format::rgba8_unorm,
            .usage = texture_usage,
            .payload = rgba_payload,
            .subresources = layouts.subspan(rgba_first_subresource),
        },
    };
    const granit::asset_tools::texture::build_desc desc{
        .dimension = granit::texture_dimension::two_dimensional,
        .width = source.levels.front().width,
        .height = source.levels.front().height,
        .depth = 1U,
        .array_layers = 1U,
        .mip_levels = static_cast<std::uint32_t>(source.levels.size()),
        .variants = variants,
    };
    const auto [status, built] = granit::asset_tools::texture::build(desc);
    if (status != granit::result::success) {
      return fail(runtime_texture_build_result::encode_failed,
                  "Granit Texture Asset Manifest 构建失败", diagnostic);
    }
    if (asset_internal::encode_texture_binary(built.manifest(), built.payload(), output,
                                              diagnostic) !=
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
