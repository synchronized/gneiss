// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/resource/mesh/mesh_binary.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {

void write_u16(std::vector<std::byte>& bytes, std::size_t offset, std::uint16_t value) {
  bytes[offset] = static_cast<std::byte>(value & 0xffU);
  bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xffU);
}

void write_u32(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value) {
  for (std::size_t index = 0; index < 4U; ++index) {
    bytes[offset + index] = static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
}

void write_u64(std::vector<std::byte>& bytes, std::size_t offset, std::uint64_t value) {
  for (std::size_t index = 0; index < 8U; ++index) {
    bytes[offset + index] = static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
}

// 独立构造固定 v3 布局，覆盖可选属性标记与缺省槽值。
bool matches_v3_layout(const std::vector<std::byte>& encoded, std::uint32_t flags) {
  constexpr std::uint16_t stride = 72;
  const std::size_t index_offset = ((80U + 3U * stride) + 15U) & ~std::size_t{15U};
  std::vector<std::byte> expected(index_offset + 12U);
  expected[0] = std::byte{'G'};
  expected[1] = std::byte{'N'};
  expected[2] = std::byte{'M'};
  expected[3] = std::byte{'S'};
  write_u16(expected, 4, 3);
  write_u16(expected, 6, 80);
  write_u32(expected, 8, flags);
  write_u32(expected, 12, 3);
  write_u32(expected, 16, 3);
  write_u16(expected, 20, stride);
  write_u16(expected, 22, 4);
  write_u64(expected, 24, 80);
  write_u64(expected, 32, index_offset);
  write_u64(expected, 40, expected.size());
  const auto put = [&](std::size_t offset, float value) {
    write_u32(expected, offset, std::bit_cast<std::uint32_t>(value));
  };
  const std::array bounds{-0.5F, -0.5F, 0.0F, 0.5F, 0.5F, 0.0F};
  for (std::size_t i = 0; i < bounds.size(); ++i)
    put(48 + i * 4, bounds[i]);
  const std::array<std::array<float, 8>, 3> vertices{{{-0.5F, -0.5F, 0, 0, 0, 0, 0, 1},
                                                      {0.5F, -0.5F, 0, 1, 0, 0, 0, 1},
                                                      {0, 0.5F, 0, 0.5F, 1, 0, 0, 1}}};
  const std::array<std::array<float, 6>, 3> extras{
      {{0, 1, 1, 0, 0, 1}, {1, 0, 0, 1, 0, 0.5F}, {0.25F, 0.75F, 0, 0, 1, 0}}};
  for (std::size_t i = 0; i < vertices.size(); ++i) {
    const auto offset = 80 + i * stride;
    for (std::size_t j = 0; j < 8; ++j)
      put(offset + 4 * j, vertices[i][j]);
    put(offset + 32, 1);
    put(offset + 44, -1);
    for (std::size_t j = 0; j < 6; ++j) {
      const bool present = j < 2 ? (flags & 1U) != 0U : (flags & 2U) != 0U;
      put(offset + 48 + 4 * j, present ? extras[i][j] : (j < 2 ? 0.0F : 1.0F));
    }
    write_u32(expected, index_offset + i * 4, static_cast<std::uint32_t>(i));
  }
  return encoded == expected;
}

} // namespace

int main() { // NOLINT(bugprone-exception-escape)
  namespace asset = gneiss::engine;
  asset::mesh_binary_data source;
  source.vertices = {
      {.position = {-0.5F, -0.5F, 0.0F}, .texcoord = {0.0F, 0.0F}, .normal = {0, 0, 1}},
      {.position = {0.5F, -0.5F, 0.0F}, .texcoord = {1.0F, 0.0F}, .normal = {0, 0, 1}},
      {.position = {0.0F, 0.5F, 0.0F}, .texcoord = {0.5F, 1.0F}, .normal = {0, 0, 1}},
  };
  source.indices = {0, 1, 2};
  source.tangents.assign(3U, {1, 0, 0, -1});
  std::vector<std::byte> encoded;
  asset::mesh_binary_diagnostic diagnostic;
  if (asset::encode_mesh_binary(source, encoded, diagnostic) !=
          asset::mesh_binary_result::success ||
      !matches_v3_layout(encoded, 0) || encoded.size() != 316U || !asset::is_mesh_binary(encoded)) {
    return 1;
  }
  asset::mesh_binary_data decoded;
  if (asset::decode_mesh_binary(encoded, decoded, diagnostic) !=
          asset::mesh_binary_result::success ||
      decoded.vertices.size() != 3U || decoded.indices != source.indices ||
      decoded.bounds.min != asset::math::vector3{-0.5F, -0.5F, 0.0F} ||
      decoded.bounds.max != asset::math::vector3{0.5F, 0.5F, 0.0F} ||
      asset::dump_mesh_binary_json(decoded).find("gneiss.mesh.debug") == std::string::npos) {
    return 2;
  }

  for (const auto version : {1U, 2U, 4U}) {
    auto unsupported = encoded;
    write_u16(unsupported, 4U, static_cast<std::uint16_t>(version));
    if (asset::decode_mesh_binary(unsupported, decoded, diagnostic) !=
            asset::mesh_binary_result::unsupported_version ||
        diagnostic.byte_offset != 4U)
      return 3;
  }
  auto bad_flags = encoded;
  write_u32(bad_flags, 8, 4);
  if (asset::decode_mesh_binary(bad_flags, decoded, diagnostic) !=
      asset::mesh_binary_result::invalid_data)
    return 17;
  auto bad_stride = encoded;
  write_u16(bad_stride, 20, 48);
  if (asset::decode_mesh_binary(bad_stride, decoded, diagnostic) !=
      asset::mesh_binary_result::invalid_data)
    return 18;
  auto invalid_magic = encoded;
  invalid_magic[0] = std::byte{'X'};
  if (asset::decode_mesh_binary(invalid_magic, decoded, diagnostic) !=
      asset::mesh_binary_result::invalid_data) {
    return 4;
  }
  auto truncated = encoded;
  truncated.pop_back();
  if (asset::decode_mesh_binary(truncated, decoded, diagnostic) !=
      asset::mesh_binary_result::invalid_data) {
    return 5;
  }
  auto invalid_offset = encoded;
  write_u64(invalid_offset, 32U, UINT64_C(160));
  if (asset::decode_mesh_binary(invalid_offset, decoded, diagnostic) !=
      asset::mesh_binary_result::invalid_data) {
    return 6;
  }
  auto non_finite = encoded;
  write_u32(non_finite, 80U, UINT32_C(0x7fc00000));
  if (asset::decode_mesh_binary(non_finite, decoded, diagnostic) !=
      asset::mesh_binary_result::invalid_data) {
    return 7;
  }
  auto invalid_index = encoded;
  write_u32(invalid_index, 304U, 3U);
  if (asset::decode_mesh_binary(invalid_index, decoded, diagnostic) !=
          asset::mesh_binary_result::invalid_data ||
      diagnostic.byte_offset != 304U) {
    return 8;
  }
  auto wrong_bounds = encoded;
  write_u32(wrong_bounds, 48, std::bit_cast<std::uint32_t>(-1.0F));
  if (asset::decode_mesh_binary(wrong_bounds, decoded, diagnostic) !=
      asset::mesh_binary_result::invalid_data)
    return 16;
  source.indices[0] = 3U;
  if (asset::encode_mesh_binary(source, encoded, diagnostic) !=
      asset::mesh_binary_result::invalid_data) {
    return 9;
  }
  source.indices[0] = 0U;
  source.tangents.assign(3U, {1.0F, 0.0F, 0.0F, -1.0F});
  if (asset::encode_mesh_binary(source, encoded, diagnostic) !=
          asset::mesh_binary_result::success ||
      asset::decode_mesh_binary(encoded, decoded, diagnostic) !=
          asset::mesh_binary_result::success ||
      decoded.tangents != source.tangents || !matches_v3_layout(encoded, 0) ||
      encoded.size() != 316U)
    return 10;
  auto invalid_tangent = encoded;
  write_u32(invalid_tangent, 124U, 0U);
  if (asset::decode_mesh_binary(invalid_tangent, decoded, diagnostic) !=
      asset::mesh_binary_result::invalid_data)
    return 11;
  source.tangents[0][2] = 1.0F;
  if (asset::encode_mesh_binary(source, encoded, diagnostic) !=
      asset::mesh_binary_result::invalid_data)
    return 12;
  source.tangents[0][2] = 0.0F;
  source.uv1 = {{0, 1}, {1, 0}, {0.25F, 0.75F}};
  source.colors = {{1, 0, 0, 1}, {0, 1, 0, 0.5F}, {0, 0, 1, 0}};
  if (asset::encode_mesh_binary(source, encoded, diagnostic) !=
          asset::mesh_binary_result::success ||
      asset::decode_mesh_binary(encoded, decoded, diagnostic) !=
          asset::mesh_binary_result::success ||
      decoded.uv1 != source.uv1 || decoded.colors != source.colors ||
      !matches_v3_layout(encoded, 3) || encoded.size() != 316U)
    return 13;
  source.colors[0][0] = 2.0F;
  if (asset::encode_mesh_binary(source, encoded, diagnostic) !=
      asset::mesh_binary_result::invalid_data)
    return 14;
  source.colors.clear();
  source.uv1.pop_back();
  if (asset::encode_mesh_binary(source, encoded, diagnostic) !=
      asset::mesh_binary_result::invalid_data)
    return 15;
  source.uv1.clear();
  source.tangents.clear();
  if (asset::encode_mesh_binary(source, encoded, diagnostic) !=
      asset::mesh_binary_result::invalid_data)
    return 19;
  source.tangents.assign(2U, {1, 0, 0, -1});
  if (asset::encode_mesh_binary(source, encoded, diagnostic) !=
      asset::mesh_binary_result::invalid_data)
    return 20;
  source.tangents.assign(3U, {1, 0, 0, -1});
  for (std::uint32_t flags = 0; flags < 4; ++flags) {
    source.uv1 = {{0, 1}, {1, 0}, {0.25F, 0.75F}};
    source.colors = {{1, 0, 0, 1}, {0, 1, 0, 0.5F}, {0, 0, 1, 0}};
    if ((flags & 1U) == 0U)
      source.uv1.clear();
    if ((flags & 2U) == 0U)
      source.colors.clear();
    if (asset::encode_mesh_binary(source, encoded, diagnostic) !=
            asset::mesh_binary_result::success ||
        !matches_v3_layout(encoded, flags) ||
        asset::decode_mesh_binary(encoded, decoded, diagnostic) !=
            asset::mesh_binary_result::success ||
        decoded.uv1 != source.uv1 || decoded.colors != source.colors)
      return 21;
  }
  return 0;
}
