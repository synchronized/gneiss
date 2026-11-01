// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/resource/texture/texture_binary.hpp"
#include <algorithm>
#include <array>
#include <span>

int main() {
  namespace asset = gneiss::engine;
  const std::array manifest = {std::byte{1}, std::byte{2}, std::byte{3}};
  const std::array payload = {std::byte{4}, std::byte{5}, std::byte{6}, std::byte{7}};
  auto encoded = asset::encode_texture_binary(manifest, payload);
  if (!encoded || encoded->size() != 84U || !asset::is_texture_binary(*encoded))
    return 1;
  auto view = asset::decode_texture_binary(*encoded);
  if (!view || !std::ranges::equal(view->manifest, manifest) ||
      !std::ranges::equal(view->payload, payload) ||
      view->manifest.data() != encoded->data() + 64U ||
      view->payload.data() != encoded->data() + 80U)
    return 2;
  auto truncated = *encoded;
  truncated.pop_back();
  auto bad = asset::decode_texture_binary(truncated);
  if (bad || bad.error().error != asset::texture_binary_error::invalid_container ||
      bad.error().message.empty())
    return 3;
  auto unsupported = *encoded;
  unsupported[8] = std::byte{2};
  bad = asset::decode_texture_binary(unsupported);
  if (bad || bad.error().error != asset::texture_binary_error::unsupported_version ||
      bad.error().message.empty())
    return 4;
  auto padding = *encoded;
  padding[67] = std::byte{1};
  bad = asset::decode_texture_binary(padding);
  if (bad || bad.error().error != asset::texture_binary_error::invalid_container)
    return 5;
  auto empty = asset::encode_texture_binary({}, payload);
  if (empty || empty.error().error != asset::texture_binary_error::invalid_argument ||
      empty.error().message.empty())
    return 6;
  empty = asset::encode_texture_binary(manifest, {});
  if (empty || empty.error().error != asset::texture_binary_error::invalid_argument)
    return 7;
  auto header = asset::decode_texture_binary_header(std::span<const std::byte>{*encoded}.first(64),
                                                    encoded->size());
  if (!header || header->manifest_offset != 64 || header->manifest_size != 3 ||
      header->payload_offset != 80 || header->payload_size != 4)
    return 8;
  auto wrong_size = asset::decode_texture_binary_header(*encoded, encoded->size() - 1);
  if (wrong_size || wrong_size.error().error != asset::texture_binary_error::invalid_container)
    return 9;
  for (std::size_t size = 0; size < 64; ++size) {
    auto short_header = asset::decode_texture_binary_header(
        std::span<const std::byte>{*encoded}.first(size), encoded->size());
    if (short_header ||
        short_header.error().error != asset::texture_binary_error::invalid_container)
      return 10;
  }
  return 0;
}
