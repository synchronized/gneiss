// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "asset/mesh_binary.h"

#include <array>
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

} // namespace

int main() { // NOLINT(bugprone-exception-escape)
  namespace asset = gneiss::asset_internal;
  asset::mesh_binary_data source;
  source.vertices = {
      {.position = {-0.5F, -0.5F, 0.0F}, .texcoord = {0.0F, 0.0F}, .normal = {0, 0, 1}},
      {.position = {0.5F, -0.5F, 0.0F}, .texcoord = {1.0F, 0.0F}, .normal = {0, 0, 1}},
      {.position = {0.0F, 0.5F, 0.0F}, .texcoord = {0.5F, 1.0F}, .normal = {0, 0, 1}},
  };
  source.indices = {0, 1, 2};
  std::vector<std::byte> encoded;
  asset::mesh_binary_diagnostic diagnostic;
  if (asset::encode_mesh_binary(source, encoded, diagnostic) !=
          asset::mesh_binary_result::success ||
      encoded.size() != 188U || !asset::is_mesh_binary(encoded)) {
    return 1;
  }
  asset::mesh_binary_data decoded;
  if (asset::decode_mesh_binary(encoded, decoded, diagnostic) !=
          asset::mesh_binary_result::success ||
      decoded.vertices.size() != 3U || decoded.indices != source.indices ||
      decoded.bounds_min != std::array<float, 3>{-0.5F, -0.5F, 0.0F} ||
      decoded.bounds_max != std::array<float, 3>{0.5F, 0.5F, 0.0F} ||
      asset::dump_mesh_binary_json(decoded).find("gneiss.mesh.debug") == std::string::npos) {
    return 2;
  }

  auto unsupported = encoded;
  write_u16(unsupported, 4U, 2U);
  if (asset::decode_mesh_binary(unsupported, decoded, diagnostic) !=
          asset::mesh_binary_result::unsupported_version ||
      diagnostic.byte_offset != 4U) {
    return 3;
  }
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
  write_u32(invalid_index, 176U, 3U);
  if (asset::decode_mesh_binary(invalid_index, decoded, diagnostic) !=
          asset::mesh_binary_result::invalid_data ||
      diagnostic.byte_offset != 176U) {
    return 8;
  }
  source.indices[0] = 3U;
  if (asset::encode_mesh_binary(source, encoded, diagnostic) !=
      asset::mesh_binary_result::invalid_data) {
    return 9;
  }
  return 0;
}
