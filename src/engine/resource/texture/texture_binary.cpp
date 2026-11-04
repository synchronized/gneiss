// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/resource/texture/texture_binary.hpp"
#include "engine/core/binary_reader.hpp"
#include "engine/core/binary_writer.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <new>
#include <string_view>
#include <utility>

namespace gneiss::engine {
namespace {

constexpr std::array<std::byte, 8U> magic = {
    std::byte{'G'}, std::byte{'N'}, std::byte{'T'}, std::byte{'E'},
    std::byte{'X'}, std::byte{'A'}, std::byte{},    std::byte{},
};
constexpr std::uint16_t schema = 1U;
constexpr std::uint16_t header_size = 64U;

[[nodiscard]] std::uint64_t align_16(std::uint64_t value) noexcept {
  return (value + 15U) & ~UINT64_C(15);
}

[[nodiscard]] texture_binary_diagnostic fail(texture_binary_error error,
                                             std::string_view message) noexcept {
  try {
    return {error, std::string{message}};
  } catch (...) {
    return {texture_binary_error::out_of_memory, {}};
  }
}

} // namespace

bool is_texture_binary(std::span<const std::byte> bytes) noexcept {
  return bytes.size() >= magic.size() && std::ranges::equal(bytes.first(magic.size()), magic);
}

result<std::vector<std::byte>, texture_binary_diagnostic>
encode_texture_binary(std::span<const std::byte> manifest,
                      std::span<const std::byte> payload) noexcept {
  if (manifest.empty() || payload.empty()) {
    return fail(texture_binary_error::invalid_argument, "运行纹理 Manifest 或 Payload 为空");
  }
  const auto manifest_offset = static_cast<std::uint64_t>(header_size);
  if (manifest.size() > std::numeric_limits<std::uint64_t>::max() - manifest_offset - 15U) {
    return fail(texture_binary_error::invalid_argument, "运行纹理 Manifest 尺寸溢出");
  }
  const auto payload_offset = align_16(manifest_offset + manifest.size());
  if (payload_offset < manifest.size() ||
      payload.size() > std::numeric_limits<std::uint64_t>::max() - payload_offset ||
      payload_offset + payload.size() > std::numeric_limits<std::size_t>::max()) {
    return fail(texture_binary_error::invalid_argument, "运行纹理尺寸溢出");
  }
  try {
    std::vector<std::byte> output;
    const auto file_size = payload_offset + payload.size();
    output.reserve(static_cast<std::size_t>(file_size));
    binary_writer writer{output};
    writer.write_bytes(magic);
    writer.write_u16_le(schema);
    writer.write_u16_le(header_size);
    writer.write_u32_le(0U);
    writer.write_u64_le(manifest_offset);
    writer.write_u64_le(manifest.size());
    writer.write_u64_le(payload_offset);
    writer.write_u64_le(payload.size());
    writer.write_u64_le(file_size);
    writer.write_u64_le(0U);
    writer.write_bytes(manifest);
    writer.write_zeros(static_cast<std::size_t>(payload_offset) - writer.position());
    writer.write_bytes(payload);
    return output;
  } catch (const std::bad_alloc&) {
    return fail(texture_binary_error::out_of_memory, "运行纹理编码内存不足");
  } catch (...) {
    return fail(texture_binary_error::invalid_argument, "运行纹理编码失败");
  }
}

result<texture_binary_layout, texture_binary_diagnostic>
decode_texture_binary_header(std::span<const std::byte> bytes, std::uint64_t source_size) noexcept {
  const binary_reader reader{bytes};
  // 各读取位置由下面的 Header、表及数据区边界检查保证有效。
  if (bytes.size() < header_size || !is_texture_binary(bytes)) {
    return fail(texture_binary_error::invalid_container, "运行纹理 Header 无效或截断");
  }
  if (*reader.read_at_le<std::uint16_t>(8U) != schema) {
    return fail(texture_binary_error::unsupported_version, "不支持的运行纹理版本");
  }
  const auto stored_header_size = *reader.read_at_le<std::uint16_t>(10U);
  const auto flags = *reader.read_at_le<std::uint32_t>(12U);
  const auto manifest_offset = *reader.read_at_le<std::uint64_t>(16U);
  const auto manifest_size = *reader.read_at_le<std::uint64_t>(24U);
  const auto payload_offset = *reader.read_at_le<std::uint64_t>(32U);
  const auto payload_size = *reader.read_at_le<std::uint64_t>(40U);
  const auto file_size = *reader.read_at_le<std::uint64_t>(48U);
  const auto reserved = *reader.read_at_le<std::uint64_t>(56U);
  const auto manifest_end_overflows =
      manifest_offset > std::numeric_limits<std::uint64_t>::max() - 15U ||
      manifest_size > std::numeric_limits<std::uint64_t>::max() - manifest_offset - 15U;
  if (stored_header_size != header_size || flags != 0U || reserved != 0U ||
      manifest_offset != header_size || manifest_size == 0U || payload_size == 0U ||
      manifest_end_overflows || payload_offset != align_16(manifest_offset + manifest_size) ||
      file_size != source_size || payload_offset > file_size ||
      payload_size != file_size - payload_offset) {
    return fail(texture_binary_error::invalid_container, "运行纹理布局或边界无效");
  }
  return texture_binary_layout{
      .manifest_offset = manifest_offset,
      .manifest_size = manifest_size,
      .payload_offset = payload_offset,
      .payload_size = payload_size,
  };
}

result<texture_binary_view, texture_binary_diagnostic>
decode_texture_binary(std::span<const std::byte> bytes) noexcept {
  auto layout = decode_texture_binary_header(bytes, bytes.size());
  if (!layout)
    return std::move(layout.error());
  const auto [manifest_offset, manifest_size, payload_offset, payload_size] = *layout;
  for (auto offset = manifest_offset + manifest_size; offset < payload_offset; ++offset) {
    if (bytes[static_cast<std::size_t>(offset)] != std::byte{})
      return fail(texture_binary_error::invalid_container, "运行纹理对齐填充非零");
  }
  return texture_binary_view{.manifest = bytes.subspan(static_cast<std::size_t>(manifest_offset),
                                                       static_cast<std::size_t>(manifest_size)),
                             .payload = bytes.subspan(static_cast<std::size_t>(payload_offset),
                                                      static_cast<std::size_t>(payload_size))};
}
} // namespace gneiss::engine
