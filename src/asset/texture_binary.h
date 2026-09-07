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

enum class texture_binary_result : std::uint8_t {
  success,
  invalid_argument,
  invalid_container,
  unsupported_version,
  out_of_memory,
};

[[nodiscard]] bool is_texture_binary(std::span<const std::byte> bytes) noexcept;
[[nodiscard]] texture_binary_result encode_texture_binary(std::span<const std::byte> manifest,
                                                          std::span<const std::byte> payload,
                                                          std::vector<std::byte>& output,
                                                          std::string& diagnostic) noexcept;
[[nodiscard]] texture_binary_result decode_texture_binary(std::span<const std::byte> bytes,
                                                          texture_binary_view& output,
                                                          std::string& diagnostic) noexcept;

} // namespace gneiss::asset_internal

#endif
