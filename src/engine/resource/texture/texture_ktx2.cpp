// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/resource/texture/texture_ktx2.hpp"
#include "engine/core/binary_reader.hpp"
#include "engine/core/binary_writer.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <new>
#include <utility>

namespace gneiss::engine {
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

[[nodiscard]] bool valid_range(std::uint64_t offset, std::uint64_t length,
                               std::size_t size) noexcept {
  return offset <= size && length <= size - static_cast<std::size_t>(offset);
}

[[nodiscard]] texture_ktx2_diagnostic fail(texture_ktx2_error error,
                                           std::string_view message) noexcept {
  try {
    return {error, std::string{message}};
  } catch (...) {
    return {texture_ktx2_error::out_of_memory, {}};
  }
}

} // namespace

result<std::vector<std::byte>, texture_ktx2_diagnostic>
encode_texture_ktx2(const texture_ktx2& texture) noexcept try {
  std::vector<std::byte> output;
  if (texture.levels.empty() || texture.levels.size() > 32U) {
    return fail(texture_ktx2_error::invalid_argument, "KTX2 Mip 数量无效");
  }
  std::uint32_t width = texture.levels.front().width;
  std::uint32_t height = texture.levels.front().height;
  for (const auto& level : texture.levels) {
    const auto expected = static_cast<std::uint64_t>(width) * height * 4U;
    if (width == 0U || height == 0U || expected != level.pixels.size() || level.width != width ||
        level.height != height) {
      return fail(texture_ktx2_error::invalid_argument, "KTX2 Mip 尺寸或字节数无效");
    }
    width = std::max(1U, width / 2U);
    height = std::max(1U, height / 2U);
  }
  try {
    const auto index_size = texture.levels.size() * level_entry_size;
    const auto dfd_offset = header_size + index_size;
    auto data_offset = dfd_offset + linear_dfd.size();
    output.reserve(data_offset + texture.levels.front().pixels.size() * 2U);
    binary_writer writer{output};
    writer.write_bytes(identifier);
    writer.write_u32_le(texture.transfer == texture_transfer::srgb ? rgba8_srgb : rgba8_unorm);
    writer.write_u32_le(1U);
    writer.write_u32_le(texture.levels.front().width);
    writer.write_u32_le(texture.levels.front().height);
    writer.write_u32_le(0U);
    writer.write_u32_le(0U);
    writer.write_u32_le(1U);
    writer.write_u32_le(static_cast<std::uint32_t>(texture.levels.size()));
    writer.write_u32_le(0U);
    writer.write_u32_le(static_cast<std::uint32_t>(dfd_offset));
    writer.write_u32_le(static_cast<std::uint32_t>(linear_dfd.size()));
    writer.write_u32_le(0U);
    writer.write_u32_le(0U);
    writer.write_u64_le(0U);
    writer.write_u64_le(0U);
    for (const auto& level : texture.levels) {
      writer.write_u64_le(data_offset);
      writer.write_u64_le(level.pixels.size());
      writer.write_u64_le(level.pixels.size());
      data_offset += level.pixels.size();
    }
    const auto descriptor = dfd(texture.transfer);
    writer.write_bytes(descriptor);
    for (const auto& level : texture.levels) {
      writer.write_bytes(level.pixels);
    }
    return output;
  } catch (const std::bad_alloc&) {
    return fail(texture_ktx2_error::out_of_memory, "KTX2 编码内存不足");
  } catch (...) {
    return fail(texture_ktx2_error::invalid_argument, "KTX2 编码失败");
  }
} catch (...) {
  return texture_ktx2_diagnostic{texture_ktx2_error::out_of_memory, {}};
}

result<texture_ktx2, texture_ktx2_diagnostic>
decode_texture_ktx2(std::span<const std::byte> bytes) noexcept try {
  texture_ktx2 output;
  const binary_reader reader{bytes};
  // 各读取位置由下面的 Header、表及数据区边界检查保证有效。
  if (bytes.size() < header_size ||
      !std::equal(identifier.begin(), identifier.end(), bytes.begin())) {
    return fail(texture_ktx2_error::invalid_container, "KTX2 标识或文件长度无效");
  }
  const auto format = *reader.read_at_le<std::uint32_t>(12U);
  const auto width = *reader.read_at_le<std::uint32_t>(20U);
  const auto height = *reader.read_at_le<std::uint32_t>(24U);
  const auto depth = *reader.read_at_le<std::uint32_t>(28U);
  const auto layers = *reader.read_at_le<std::uint32_t>(32U);
  const auto faces = *reader.read_at_le<std::uint32_t>(36U);
  const auto levels = *reader.read_at_le<std::uint32_t>(40U);
  const auto supercompression = *reader.read_at_le<std::uint32_t>(44U);
  const auto dfd_offset = *reader.read_at_le<std::uint32_t>(48U);
  const auto dfd_length = *reader.read_at_le<std::uint32_t>(52U);
  if ((format != rgba8_unorm && format != rgba8_srgb) || depth != 0U || layers != 0U ||
      faces != 1U || supercompression != 0U) {
    return fail(texture_ktx2_error::unsupported_container, "KTX2 格式、维度或压缩方案不受支持");
  }
  std::uint32_t expected_levels = 1U;
  for (auto size = std::max(width, height); size > 1U; size /= 2U) {
    ++expected_levels;
  }
  if (width == 0U || height == 0U || levels == 0U || levels > 32U || levels != expected_levels ||
      dfd_length < linear_dfd.size() || !valid_range(dfd_offset, dfd_length, bytes.size()) ||
      *reader.read_at_le<std::uint32_t>(dfd_offset) > dfd_length ||
      bytes.size() < header_size + static_cast<std::size_t>(levels) * level_entry_size) {
    return fail(texture_ktx2_error::invalid_container, "KTX2 维度或 Mip 数量无效");
  }
  const auto transfer = std::to_integer<std::uint8_t>(bytes[dfd_offset + 14U]);
  if ((format == rgba8_srgb && transfer != 2U) || (format == rgba8_unorm && transfer != 1U)) {
    return fail(texture_ktx2_error::invalid_container, "KTX2 格式与传递函数不一致");
  }
  try {
    output.transfer = format == rgba8_srgb ? texture_transfer::srgb : texture_transfer::linear;
    output.levels.reserve(levels);
    auto level_width = width;
    auto level_height = height;
    for (std::uint32_t level = 0U; level < levels; ++level) {
      const auto entry = header_size + static_cast<std::size_t>(level) * level_entry_size;
      const auto offset = *reader.read_at_le<std::uint64_t>(entry);
      const auto length = *reader.read_at_le<std::uint64_t>(entry + 8U);
      const auto unpacked = *reader.read_at_le<std::uint64_t>(entry + 16U);
      const auto expected = static_cast<std::uint64_t>(level_width) * level_height * 4U;
      if (length != expected || unpacked != expected ||
          !valid_range(offset, length, bytes.size())) {
        return fail(texture_ktx2_error::invalid_container, "KTX2 Mip 数据范围或长度无效");
      }
      const auto begin = bytes.begin() + static_cast<std::ptrdiff_t>(offset);
      output.levels.push_back({.width = level_width,
                               .height = level_height,
                               .pixels = {begin, begin + static_cast<std::ptrdiff_t>(length)}});
      level_width = std::max(1U, level_width / 2U);
      level_height = std::max(1U, level_height / 2U);
    }
    return output;
  } catch (const std::bad_alloc&) {
    return fail(texture_ktx2_error::out_of_memory, "KTX2 解码内存不足");
  } catch (...) {
    return fail(texture_ktx2_error::invalid_container, "KTX2 解码失败");
  }
} catch (...) {
  return texture_ktx2_diagnostic{texture_ktx2_error::out_of_memory, {}};
}

} // namespace gneiss::engine
