// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "tooling/asset_build/ktx2_probe.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace gneiss::tooling::asset_build {
namespace {

constexpr std::array<std::uint8_t, 12U> identifier = {0xABU, 0x4BU, 0x54U, 0x58U, 0x20U, 0x32U,
                                                      0x30U, 0xBBU, 0x0DU, 0x0AU, 0x1AU, 0x0AU};
constexpr std::size_t header_size = 80U;
constexpr std::size_t level_entry_size = 24U;

[[nodiscard]] std::uint32_t read_u32(std::span<const std::uint8_t> bytes,
                                     std::size_t offset) noexcept {
  return static_cast<std::uint32_t>(bytes[offset]) |
         (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
         (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
         (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

[[nodiscard]] std::uint64_t read_u64(std::span<const std::uint8_t> bytes,
                                     std::size_t offset) noexcept {
  return static_cast<std::uint64_t>(read_u32(bytes, offset)) |
         (static_cast<std::uint64_t>(read_u32(bytes, offset + 4U)) << 32U);
}

[[nodiscard]] ktx2_probe_report fail(ktx2_probe_result result, std::string diagnostic) {
  return {.result = result, .information = {}, .diagnostic = std::move(diagnostic)};
}

[[nodiscard]] bool valid_range(std::uint64_t offset, std::uint64_t length,
                               std::size_t size) noexcept {
  return offset <= size && length <= size - static_cast<std::size_t>(offset);
}

} // namespace

ktx2_probe_report inspect_ktx2(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    return fail(ktx2_probe_result::not_found, "无法打开 KTX2 文件");
  }
  const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>{stream},
                                        std::istreambuf_iterator<char>{}};
  if (stream.bad() || bytes.size() < header_size ||
      !std::equal(identifier.begin(), identifier.end(), bytes.begin())) {
    return fail(ktx2_probe_result::invalid_container, "KTX2 标识或文件长度无效");
  }
  const std::span data(bytes);
  ktx2_information information{
      .vk_format = read_u32(data, 12U),
      .width = read_u32(data, 20U),
      .height = read_u32(data, 24U),
      .depth = read_u32(data, 28U),
      .layer_count = read_u32(data, 32U),
      .face_count = read_u32(data, 36U),
      .level_count = read_u32(data, 40U),
  };
  const auto scheme = read_u32(data, 44U);
  switch (scheme) {
  case 0U:
    information.supercompression = ktx2_supercompression::none;
    break;
  case 1U:
    information.supercompression = ktx2_supercompression::basis_lz;
    break;
  case 2U:
    information.supercompression = ktx2_supercompression::zstd;
    break;
  case 3U:
    information.supercompression = ktx2_supercompression::zlib;
    break;
  default:
    return fail(ktx2_probe_result::unsupported_container, "KTX2 超级压缩方案未知");
  }
  bool level_table_fits = true;
  if constexpr (sizeof(std::size_t) < sizeof(std::uint64_t)) {
    level_table_fits = static_cast<std::uint64_t>(information.level_count) <=
                       (std::numeric_limits<std::size_t>::max() - header_size) / level_entry_size;
  }
  if (information.width == 0U || information.face_count == 0U ||
      (information.face_count != 1U && information.face_count != 6U) ||
      information.level_count == 0U || !level_table_fits ||
      bytes.size() < header_size + information.level_count * level_entry_size) {
    return fail(ktx2_probe_result::invalid_container, "KTX2 维度、面数或 Mip 数量无效");
  }
  for (std::size_t level = 0U; level < information.level_count; ++level) {
    const auto entry = header_size + level * level_entry_size;
    const auto offset = read_u64(data, entry);
    const auto length = read_u64(data, entry + 8U);
    const auto uncompressed_length = read_u64(data, entry + 16U);
    if (length == 0U || uncompressed_length == 0U || !valid_range(offset, length, bytes.size())) {
      return fail(ktx2_probe_result::invalid_container, "KTX2 Mip 数据范围无效");
    }
  }

  // Vulkan 的 R8G8B8A8_UNORM/SRGB；这里只识别 Spike 所需的代表格式。
  if (information.vk_format == 37U) {
    information.transfer_function = ktx2_transfer_function::linear;
    information.has_alpha = true;
  } else if (information.vk_format == 43U) {
    information.transfer_function = ktx2_transfer_function::srgb;
    information.has_alpha = true;
  }
  return {.result = ktx2_probe_result::success, .information = information, .diagnostic = {}};
}

} // namespace gneiss::tooling::asset_build
