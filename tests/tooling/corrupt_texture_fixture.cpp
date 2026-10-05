// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/asset/texture_binary.hpp"

#include <granit/renderer/texture_asset.hpp>

#include <fstream>
#include <string>
#include <vector>

// 仅用于端到端验收：保持封装和 Manifest 有效，破坏所有变体摘要，覆盖不同设备的选择结果。
int main(int argc, char** argv) { // NOLINT(bugprone-exception-escape)
  if (argc != 2) {
    return 2;
  }
  std::ifstream input(argv[1], std::ios::binary);
  input.seekg(0, std::ios::end);
  if (!input || input.tellg() <= 0) {
    return 1;
  }
  const auto size = input.tellg();
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  input.seekg(0);
  input.read(reinterpret_cast<char*>(bytes.data()), size);
  if (!input) {
    return 1;
  }
  input.close();
  gneiss::asset_internal::texture_binary_view binary;
  std::string diagnostic;
  if (gneiss::asset_internal::decode_texture_binary(bytes, binary, diagnostic) !=
          gneiss::asset_internal::texture_binary_result::success ||
      binary.payload.empty()) {
    return 1;
  }
  granit::texture_asset_info info;
  if (granit::inspect_texture_asset(binary.manifest, info).failed()) {
    return 1;
  }
  for (const auto& variant : info.variants) {
    if (variant.payload_offset >= binary.payload.size()) {
      return 1;
    }
    bytes[static_cast<std::size_t>(binary.payload.data() - bytes.data()) +
          static_cast<std::size_t>(variant.payload_offset)] ^= std::byte{1U};
  }
  std::ofstream output(argv[1], std::ios::binary);
  output.write(reinterpret_cast<const char*>(bytes.data()), size);
  output.close();
  return output ? 0 : 1;
}
