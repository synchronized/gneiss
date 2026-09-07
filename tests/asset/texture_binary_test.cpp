// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "asset/texture_binary.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <vector>

int main() { // NOLINT(bugprone-exception-escape)
  namespace asset = gneiss::asset_internal;
  const std::array manifest = {std::byte{1}, std::byte{2}, std::byte{3}};
  const std::array payload = {std::byte{4}, std::byte{5}, std::byte{6}, std::byte{7}};
  std::vector<std::byte> encoded;
  std::string diagnostic;
  if (asset::encode_texture_binary(manifest, payload, encoded, diagnostic) !=
          asset::texture_binary_result::success ||
      encoded.size() != 84U || !asset::is_texture_binary(encoded)) {
    return 1;
  }
  asset::texture_binary_view view;
  if (asset::decode_texture_binary(encoded, view, diagnostic) !=
          asset::texture_binary_result::success ||
      !std::ranges::equal(view.manifest, manifest) || !std::ranges::equal(view.payload, payload)) {
    return 2;
  }
  auto truncated = encoded;
  truncated.pop_back();
  if (asset::decode_texture_binary(truncated, view, diagnostic) !=
      asset::texture_binary_result::invalid_container) {
    return 3;
  }
  auto unsupported = encoded;
  unsupported[8] = std::byte{2};
  if (asset::decode_texture_binary(unsupported, view, diagnostic) !=
      asset::texture_binary_result::unsupported_version) {
    return 4;
  }
  auto invalid_padding = encoded;
  invalid_padding[67] = std::byte{1};
  if (asset::decode_texture_binary(invalid_padding, view, diagnostic) !=
      asset::texture_binary_result::invalid_container) {
    return 5;
  }
  return 0;
}
