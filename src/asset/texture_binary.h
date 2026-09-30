// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_ASSET_TEXTURE_BINARY_H_
#define GNEISS_ASSET_TEXTURE_BINARY_H_

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace gneiss::asset_internal {

struct texture_binary_view final {
  std::span<const std::byte> manifest;
  std::span<const std::byte> payload;
};

/** 已校验的封装布局；偏移均相对于文件起点，不包含 Manifest 内部语义。 */
struct texture_binary_layout final {
  std::uint64_t manifest_offset{};
  std::uint64_t manifest_size{};
  std::uint64_t payload_offset{};
  std::uint64_t payload_size{};
};

enum class texture_binary_result : std::uint8_t {
  success,
  invalid_argument,
  invalid_container,
  unsupported_version,
  out_of_memory,
};

[[nodiscard]] bool is_texture_binary(std::span<const std::byte> bytes) noexcept;
/** 仅解析 64 字节头；source_size 为打开来源的完整长度，不读取负载或校验填充。 */
[[nodiscard]] texture_binary_result decode_texture_binary_header(std::span<const std::byte> bytes,
                                                                 std::uint64_t source_size,
                                                                 texture_binary_layout& output,
                                                                 std::string& diagnostic) noexcept;
[[nodiscard]] texture_binary_result encode_texture_binary(std::span<const std::byte> manifest,
                                                          std::span<const std::byte> payload,
                                                          std::vector<std::byte>& output,
                                                          std::string& diagnostic) noexcept;
[[nodiscard]] texture_binary_result decode_texture_binary(std::span<const std::byte> bytes,
                                                          texture_binary_view& output,
                                                          std::string& diagnostic) noexcept;

} // namespace gneiss::asset_internal

#endif
