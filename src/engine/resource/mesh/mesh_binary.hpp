// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_ASSET_MESH_BINARY_HPP_
#define GNEISS_ASSET_MESH_BINARY_HPP_

#include "engine/core/math/aabb.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace gneiss::engine {

struct mesh_binary_vertex final {
  math::vector3 position{};
  math::vector2 texcoord{};
  math::vector3 normal{};
};

struct mesh_binary_data final {
  std::vector<mesh_binary_vertex> vertices;
  std::vector<std::uint32_t> indices;
  /** 解码得到的局部包围盒；编码时根据顶点重算，不信任此字段。 */
  math::aabb bounds{};
  /** 为空时编码 v1；非空时编码 v2，存在 UV1/颜色时编码 v3。 */
  std::vector<math::vector4> tangents{};
  std::vector<math::vector2> uv1{};
  std::vector<math::vector4> colors{};
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

} // namespace gneiss::engine

#endif
