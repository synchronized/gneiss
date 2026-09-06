// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_ASSET_TEXTURE_KTX2_H_
#define GNEISS_ASSET_TEXTURE_KTX2_H_

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace gneiss::asset_internal {

enum class texture_transfer : std::uint8_t { linear, srgb };

struct texture_mip final {
  std::uint32_t width{};
  std::uint32_t height{};
  std::vector<std::byte> pixels;
};

struct texture_ktx2 final {
  texture_transfer transfer{texture_transfer::srgb};
  std::vector<texture_mip> levels;
};

enum class texture_ktx2_result : std::uint8_t {
  success,
  invalid_argument,
  invalid_container,
  unsupported_container,
  out_of_memory,
};

/** 将二维 RGBA8 完整 Mip 链写为无超级压缩 KTX2。 */
[[nodiscard]] texture_ktx2_result encode_texture_ktx2(const texture_ktx2& texture,
                                                      std::vector<std::byte>& output,
                                                      std::string& diagnostic) noexcept;

/** 解析 Gneiss 支持的二维 RGBA8 KTX2，并复制所有 Mip 数据。 */
[[nodiscard]] texture_ktx2_result decode_texture_ktx2(std::span<const std::byte> bytes,
                                                      texture_ktx2& output,
                                                      std::string& diagnostic) noexcept;

} // namespace gneiss::asset_internal

#endif
