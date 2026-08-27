// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_ASSET_MESH_BINARY_H_
#define GNEISS_ASSET_MESH_BINARY_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace gneiss::asset_internal {

struct mesh_binary_vertex final {
  std::array<float, 3> position{};
  std::array<float, 2> texcoord{};
  std::array<float, 3> normal{};
};

struct mesh_binary_data final {
  std::vector<mesh_binary_vertex> vertices;
  std::vector<std::uint32_t> indices;
  std::array<float, 3> bounds_min{};
  std::array<float, 3> bounds_max{};
};

enum class mesh_binary_result : std::uint8_t { success, invalid_data, unsupported_version };

struct mesh_binary_diagnostic final {
  mesh_binary_result result = mesh_binary_result::success;
  std::size_t byte_offset{};
  std::string message;
};

[[nodiscard]] bool is_mesh_binary(std::span<const std::byte> bytes) noexcept;
[[nodiscard]] mesh_binary_result encode_mesh_binary(const mesh_binary_data& data,
                                                    std::vector<std::byte>& output,
                                                    mesh_binary_diagnostic& diagnostic) noexcept;
[[nodiscard]] mesh_binary_result decode_mesh_binary(std::span<const std::byte> bytes,
                                                    mesh_binary_data& output,
                                                    mesh_binary_diagnostic& diagnostic) noexcept;
[[nodiscard]] std::string dump_mesh_binary_json(const mesh_binary_data& data);

} // namespace gneiss::asset_internal

#endif
