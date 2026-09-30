// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "asset/texture_binary.h"

#include <algorithm>
#include <array>
#include <limits>
#include <new>
#include <string_view>
#include <utility>

namespace gneiss::asset_internal {
namespace {

constexpr std::array<std::byte, 8U> magic = {
    std::byte{'G'}, std::byte{'N'}, std::byte{'T'}, std::byte{'E'},
    std::byte{'X'}, std::byte{'A'}, std::byte{},    std::byte{},
};
constexpr std::uint16_t schema = 1U;
constexpr std::uint16_t header_size = 64U;

void append_u16(std::vector<std::byte>& output, std::uint16_t value) {
  output.push_back(static_cast<std::byte>(value & 0xffU));
  output.push_back(static_cast<std::byte>((static_cast<std::uint32_t>(value) >> 8U) & 0xffU));
}

void append_u32(std::vector<std::byte>& output, std::uint32_t value) {
  for (std::uint32_t shift = 0U; shift < 32U; shift += 8U) {
    output.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
  }
}

void append_u64(std::vector<std::byte>& output, std::uint64_t value) {
  append_u32(output, static_cast<std::uint32_t>(value));
  append_u32(output, static_cast<std::uint32_t>(value >> 32U));
}

template <typename Integer>
[[nodiscard]] Integer read_integer(std::span<const std::byte> bytes, std::size_t offset) noexcept {
  std::uint64_t value{};
  for (std::size_t index = 0U; index < sizeof(Integer); ++index) {
    value |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset + index]))
             << (index * 8U);
  }
  return static_cast<Integer>(value);
}

[[nodiscard]] std::uint64_t align_16(std::uint64_t value) noexcept {
  return (value + 15U) & ~UINT64_C(15);
}

[[nodiscard]] texture_binary_result fail(texture_binary_result result, std::string_view message,
                                         std::string& diagnostic) noexcept {
  try {
    diagnostic.assign(message);
  } catch (...) {
    diagnostic.clear();
    return texture_binary_result::out_of_memory;
  }
  return result;
}

} // namespace

bool is_texture_binary(std::span<const std::byte> bytes) noexcept {
  return bytes.size() >= magic.size() && std::ranges::equal(bytes.first(magic.size()), magic);
}

texture_binary_result encode_texture_binary(std::span<const std::byte> manifest,
                                            std::span<const std::byte> payload,
                                            std::vector<std::byte>& output,
                                            std::string& diagnostic) noexcept {
  output.clear();
  diagnostic.clear();
  if (manifest.empty() || payload.empty()) {
    return fail(texture_binary_result::invalid_argument, "运行纹理 Manifest 或 Payload 为空",
                diagnostic);
  }
  const auto manifest_offset = static_cast<std::uint64_t>(header_size);
  if (manifest.size() > std::numeric_limits<std::uint64_t>::max() - manifest_offset - 15U) {
    return fail(texture_binary_result::invalid_argument, "运行纹理 Manifest 尺寸溢出", diagnostic);
  }
  const auto payload_offset = align_16(manifest_offset + manifest.size());
  if (payload_offset < manifest.size() ||
      payload.size() > std::numeric_limits<std::uint64_t>::max() - payload_offset ||
      payload_offset + payload.size() > std::numeric_limits<std::size_t>::max()) {
    return fail(texture_binary_result::invalid_argument, "运行纹理尺寸溢出", diagnostic);
  }
  try {
    const auto file_size = payload_offset + payload.size();
    output.reserve(static_cast<std::size_t>(file_size));
    output.insert(output.end(), magic.begin(), magic.end());
    append_u16(output, schema);
    append_u16(output, header_size);
    append_u32(output, 0U);
    append_u64(output, manifest_offset);
    append_u64(output, manifest.size());
    append_u64(output, payload_offset);
    append_u64(output, payload.size());
    append_u64(output, file_size);
    append_u64(output, 0U);
    output.insert(output.end(), manifest.begin(), manifest.end());
    output.resize(static_cast<std::size_t>(payload_offset), std::byte{});
    output.insert(output.end(), payload.begin(), payload.end());
    return texture_binary_result::success;
  } catch (const std::bad_alloc&) {
    output.clear();
    return fail(texture_binary_result::out_of_memory, "运行纹理编码内存不足", diagnostic);
  } catch (...) {
    output.clear();
    return fail(texture_binary_result::invalid_argument, "运行纹理编码失败", diagnostic);
  }
}

texture_binary_result decode_texture_binary_header(std::span<const std::byte> bytes,
                                                   std::uint64_t source_size,
                                                   texture_binary_layout& output,
                                                   std::string& diagnostic) noexcept {
  output = {};
  diagnostic.clear();
  if (bytes.size() < header_size || !is_texture_binary(bytes)) {
    return fail(texture_binary_result::invalid_container, "运行纹理 Header 无效或截断", diagnostic);
  }
  if (read_integer<std::uint16_t>(bytes, 8U) != schema) {
    return fail(texture_binary_result::unsupported_version, "不支持的运行纹理版本", diagnostic);
  }
  const auto stored_header_size = read_integer<std::uint16_t>(bytes, 10U);
  const auto flags = read_integer<std::uint32_t>(bytes, 12U);
  const auto manifest_offset = read_integer<std::uint64_t>(bytes, 16U);
  const auto manifest_size = read_integer<std::uint64_t>(bytes, 24U);
  const auto payload_offset = read_integer<std::uint64_t>(bytes, 32U);
  const auto payload_size = read_integer<std::uint64_t>(bytes, 40U);
  const auto file_size = read_integer<std::uint64_t>(bytes, 48U);
  const auto reserved = read_integer<std::uint64_t>(bytes, 56U);
  const auto manifest_end_overflows =
      manifest_offset > std::numeric_limits<std::uint64_t>::max() - 15U ||
      manifest_size > std::numeric_limits<std::uint64_t>::max() - manifest_offset - 15U;
  if (stored_header_size != header_size || flags != 0U || reserved != 0U ||
      manifest_offset != header_size || manifest_size == 0U || payload_size == 0U ||
      manifest_end_overflows || payload_offset != align_16(manifest_offset + manifest_size) ||
      file_size != source_size || payload_offset > file_size ||
      payload_size != file_size - payload_offset) {
    return fail(texture_binary_result::invalid_container, "运行纹理布局或边界无效", diagnostic);
  }
  output = {
      .manifest_offset = manifest_offset,
      .manifest_size = manifest_size,
      .payload_offset = payload_offset,
      .payload_size = payload_size,
  };
  return texture_binary_result::success;
}

texture_binary_result decode_texture_binary(std::span<const std::byte> bytes,
                                            texture_binary_view& output,
                                            std::string& diagnostic) noexcept {
  output = {};
  texture_binary_layout layout;
  const auto status = decode_texture_binary_header(bytes, bytes.size(), layout, diagnostic);
  if (status != texture_binary_result::success) {
    return status;
  }
  const auto [manifest_offset, manifest_size, payload_offset, payload_size] = layout;
  for (auto offset = manifest_offset + manifest_size; offset < payload_offset; ++offset) {
    if (bytes[static_cast<std::size_t>(offset)] != std::byte{}) {
      return fail(texture_binary_result::invalid_container, "运行纹理对齐填充非零", diagnostic);
    }
  }
  output.manifest = bytes.subspan(static_cast<std::size_t>(manifest_offset),
                                  static_cast<std::size_t>(manifest_size));
  output.payload = bytes.subspan(static_cast<std::size_t>(payload_offset),
                                 static_cast<std::size_t>(payload_size));
  return texture_binary_result::success;
}

} // namespace gneiss::asset_internal
