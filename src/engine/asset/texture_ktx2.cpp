// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/asset/texture_ktx2.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <new>
#include <utility>

namespace gneiss::asset_internal {
namespace {

constexpr std::array<std::byte, 12U> identifier = {
    std::byte{0xAB}, std::byte{0x4B}, std::byte{0x54}, std::byte{0x58},
    std::byte{0x20}, std::byte{0x32}, std::byte{0x30}, std::byte{0xBB},
    std::byte{0x0D}, std::byte{0x0A}, std::byte{0x1A}, std::byte{0x0A}};
constexpr std::size_t header_size = 80U;
constexpr std::size_t level_entry_size = 24U;
constexpr std::uint32_t rgba8_unorm = 37U;
constexpr std::uint32_t rgba8_srgb = 43U;

constexpr std::array<std::byte, 92U> linear_dfd = {
    std::byte{0x5C}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x02}, std::byte{0x00},
    std::byte{0x58}, std::byte{0x00}, std::byte{0x01}, std::byte{0x01}, std::byte{0x01},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x04}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x07}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    std::byte{0xFF}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x08},
    std::byte{0x00}, std::byte{0x07}, std::byte{0x01}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x00}, std::byte{0xFF}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x10}, std::byte{0x00}, std::byte{0x07}, std::byte{0x02}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x00}, std::byte{0xFF}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x18}, std::byte{0x00}, std::byte{0x07}, std::byte{0x0F},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0xFF}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x00}};

[[nodiscard]] std::array<std::byte, 92U> dfd(texture_transfer transfer) {
  auto value = linear_dfd;
  if (transfer == texture_transfer::srgb) {
    value[14] = std::byte{0x02};
    value[87] = std::byte{0x1F};
  }
  return value;
}

void append_u32(std::vector<std::byte>& bytes, std::uint32_t value) {
  for (std::uint32_t shift = 0; shift < 32U; shift += 8U) {
    bytes.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
  }
}

void append_u64(std::vector<std::byte>& bytes, std::uint64_t value) {
  append_u32(bytes, static_cast<std::uint32_t>(value));
  append_u32(bytes, static_cast<std::uint32_t>(value >> 32U));
}

[[nodiscard]] std::uint32_t read_u32(std::span<const std::byte> bytes,
                                     std::size_t offset) noexcept {
  std::uint32_t value{};
  for (std::uint32_t index = 0; index < 4U; ++index) {
    value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + index]))
             << (index * 8U);
  }
  return value;
}

[[nodiscard]] std::uint64_t read_u64(std::span<const std::byte> bytes,
                                     std::size_t offset) noexcept {
  return static_cast<std::uint64_t>(read_u32(bytes, offset)) |
         (static_cast<std::uint64_t>(read_u32(bytes, offset + 4U)) << 32U);
}

[[nodiscard]] bool valid_range(std::uint64_t offset, std::uint64_t length,
                               std::size_t size) noexcept {
  return offset <= size && length <= size - static_cast<std::size_t>(offset);
}

[[nodiscard]] texture_ktx2_result fail(texture_ktx2_result result, std::string message,
                                       std::string& diagnostic) noexcept {
  try {
    diagnostic = std::move(message);
  } catch (...) {
    diagnostic.clear();
    return texture_ktx2_result::out_of_memory;
  }
  return result;
}

} // namespace

texture_ktx2_result encode_texture_ktx2(const texture_ktx2& texture, std::vector<std::byte>& output,
                                        std::string& diagnostic) noexcept {
  output.clear();
  diagnostic.clear();
  if (texture.levels.empty() || texture.levels.size() > 32U) {
    return fail(texture_ktx2_result::invalid_argument, "KTX2 Mip 数量无效", diagnostic);
  }
  std::uint32_t width = texture.levels.front().width;
  std::uint32_t height = texture.levels.front().height;
  for (const auto& level : texture.levels) {
    const auto expected = static_cast<std::uint64_t>(width) * height * 4U;
    if (width == 0U || height == 0U || expected != level.pixels.size() || level.width != width ||
        level.height != height) {
      return fail(texture_ktx2_result::invalid_argument, "KTX2 Mip 尺寸或字节数无效", diagnostic);
    }
    width = std::max(1U, width / 2U);
    height = std::max(1U, height / 2U);
  }
  try {
    const auto index_size = texture.levels.size() * level_entry_size;
    const auto dfd_offset = header_size + index_size;
    auto data_offset = dfd_offset + linear_dfd.size();
    output.reserve(data_offset + texture.levels.front().pixels.size() * 2U);
    output.insert(output.end(), identifier.begin(), identifier.end());
    append_u32(output, texture.transfer == texture_transfer::srgb ? rgba8_srgb : rgba8_unorm);
    append_u32(output, 1U);
    append_u32(output, texture.levels.front().width);
    append_u32(output, texture.levels.front().height);
    append_u32(output, 0U);
    append_u32(output, 0U);
    append_u32(output, 1U);
    append_u32(output, static_cast<std::uint32_t>(texture.levels.size()));
    append_u32(output, 0U);
    append_u32(output, static_cast<std::uint32_t>(dfd_offset));
    append_u32(output, static_cast<std::uint32_t>(linear_dfd.size()));
    append_u32(output, 0U);
    append_u32(output, 0U);
    append_u64(output, 0U);
    append_u64(output, 0U);
    for (const auto& level : texture.levels) {
      append_u64(output, data_offset);
      append_u64(output, level.pixels.size());
      append_u64(output, level.pixels.size());
      data_offset += level.pixels.size();
    }
    const auto descriptor = dfd(texture.transfer);
    output.insert(output.end(), descriptor.begin(), descriptor.end());
    for (const auto& level : texture.levels) {
      output.insert(output.end(), level.pixels.begin(), level.pixels.end());
    }
    return texture_ktx2_result::success;
  } catch (const std::bad_alloc&) {
    output.clear();
    return fail(texture_ktx2_result::out_of_memory, "KTX2 编码内存不足", diagnostic);
  } catch (...) {
    output.clear();
    return fail(texture_ktx2_result::invalid_argument, "KTX2 编码失败", diagnostic);
  }
}

texture_ktx2_result decode_texture_ktx2(std::span<const std::byte> bytes, texture_ktx2& output,
                                        std::string& diagnostic) noexcept {
  output = {};
  diagnostic.clear();
  if (bytes.size() < header_size ||
      !std::equal(identifier.begin(), identifier.end(), bytes.begin())) {
    return fail(texture_ktx2_result::invalid_container, "KTX2 标识或文件长度无效", diagnostic);
  }
  const auto format = read_u32(bytes, 12U);
  const auto width = read_u32(bytes, 20U);
  const auto height = read_u32(bytes, 24U);
  const auto depth = read_u32(bytes, 28U);
  const auto layers = read_u32(bytes, 32U);
  const auto faces = read_u32(bytes, 36U);
  const auto levels = read_u32(bytes, 40U);
  const auto supercompression = read_u32(bytes, 44U);
  const auto dfd_offset = read_u32(bytes, 48U);
  const auto dfd_length = read_u32(bytes, 52U);
  if ((format != rgba8_unorm && format != rgba8_srgb) || depth != 0U || layers != 0U ||
      faces != 1U || supercompression != 0U) {
    return fail(texture_ktx2_result::unsupported_container, "KTX2 格式、维度或压缩方案不受支持",
                diagnostic);
  }
  std::uint32_t expected_levels = 1U;
  for (auto size = std::max(width, height); size > 1U; size /= 2U) {
    ++expected_levels;
  }
  if (width == 0U || height == 0U || levels == 0U || levels > 32U || levels != expected_levels ||
      dfd_length < linear_dfd.size() || !valid_range(dfd_offset, dfd_length, bytes.size()) ||
      read_u32(bytes, dfd_offset) > dfd_length ||
      bytes.size() < header_size + static_cast<std::size_t>(levels) * level_entry_size) {
    return fail(texture_ktx2_result::invalid_container, "KTX2 维度或 Mip 数量无效", diagnostic);
  }
  const auto transfer = std::to_integer<std::uint8_t>(bytes[dfd_offset + 14U]);
  if ((format == rgba8_srgb && transfer != 2U) || (format == rgba8_unorm && transfer != 1U)) {
    return fail(texture_ktx2_result::invalid_container, "KTX2 格式与传递函数不一致", diagnostic);
  }
  try {
    output.transfer = format == rgba8_srgb ? texture_transfer::srgb : texture_transfer::linear;
    output.levels.reserve(levels);
    auto level_width = width;
    auto level_height = height;
    for (std::uint32_t level = 0U; level < levels; ++level) {
      const auto entry = header_size + static_cast<std::size_t>(level) * level_entry_size;
      const auto offset = read_u64(bytes, entry);
      const auto length = read_u64(bytes, entry + 8U);
      const auto unpacked = read_u64(bytes, entry + 16U);
      const auto expected = static_cast<std::uint64_t>(level_width) * level_height * 4U;
      if (length != expected || unpacked != expected ||
          !valid_range(offset, length, bytes.size())) {
        output = {};
        return fail(texture_ktx2_result::invalid_container, "KTX2 Mip 数据范围或长度无效",
                    diagnostic);
      }
      const auto begin = bytes.begin() + static_cast<std::ptrdiff_t>(offset);
      output.levels.push_back({.width = level_width,
                               .height = level_height,
                               .pixels = {begin, begin + static_cast<std::ptrdiff_t>(length)}});
      level_width = std::max(1U, level_width / 2U);
      level_height = std::max(1U, level_height / 2U);
    }
    return texture_ktx2_result::success;
  } catch (const std::bad_alloc&) {
    output = {};
    return fail(texture_ktx2_result::out_of_memory, "KTX2 解码内存不足", diagnostic);
  } catch (...) {
    output = {};
    return fail(texture_ktx2_result::invalid_container, "KTX2 解码失败", diagnostic);
  }
}

} // namespace gneiss::asset_internal
