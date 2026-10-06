// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/asset/texture_ktx2.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace {

void write_u32(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value) {
  for (std::uint32_t index = 0; index < 4U; ++index) {
    bytes[offset + index] = static_cast<std::byte>((value >> (index * 8U)) & 0xFFU);
  }
}

} // namespace

int main() {
  using namespace gneiss::asset_internal;
  texture_ktx2 source{
      .transfer = texture_transfer::srgb,
      .levels = {
          {.width = 3U, .height = 5U, .pixels = std::vector<std::byte>(60U, std::byte{0x7F})},
          {.width = 1U, .height = 2U, .pixels = std::vector<std::byte>(8U, std::byte{0x55})},
          {.width = 1U, .height = 1U, .pixels = std::vector<std::byte>(4U, std::byte{0x22})}}};
  std::vector<std::byte> first;
  std::vector<std::byte> second;
  std::string diagnostic;
  if (encode_texture_ktx2(source, first, diagnostic) != texture_ktx2_result::success ||
      encode_texture_ktx2(source, second, diagnostic) != texture_ktx2_result::success ||
      first != second || !diagnostic.empty()) {
    return 1;
  }
  texture_ktx2 decoded;
  if (decode_texture_ktx2(first, decoded, diagnostic) != texture_ktx2_result::success ||
      decoded.transfer != texture_transfer::srgb || decoded.levels.size() != 3U ||
      decoded.levels[1U].width != 1U || decoded.levels[1U].height != 2U ||
      decoded.levels[2U].pixels != source.levels[2U].pixels) {
    return 2;
  }

  auto invalid_format = first;
  write_u32(invalid_format, 12U, 0U);
  if (decode_texture_ktx2(invalid_format, decoded, diagnostic) !=
      texture_ktx2_result::unsupported_container) {
    return 3;
  }
  auto compressed = first;
  write_u32(compressed, 44U, 2U);
  if (decode_texture_ktx2(compressed, decoded, diagnostic) !=
      texture_ktx2_result::unsupported_container) {
    return 4;
  }
  auto truncated = first;
  truncated.pop_back();
  if (decode_texture_ktx2(truncated, decoded, diagnostic) !=
      texture_ktx2_result::invalid_container) {
    return 5;
  }
  return 0;
}
