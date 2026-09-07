// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "tooling/asset_build/sha256.h"

#include <array>
#include <cstddef>
#include <string_view>

namespace {

[[nodiscard]] bool equals_hex(const gneiss::tooling::asset_build::sha256_digest& digest,
                              std::string_view expected) {
  constexpr std::string_view digits = "0123456789abcdef";
  for (std::size_t index = 0U; index < digest.size(); ++index) {
    const auto value = std::to_integer<unsigned char>(digest[index]);
    if (expected[index * 2U] != digits[value >> 4U] ||
        expected[index * 2U + 1U] != digits[value & 0x0fU]) {
      return false;
    }
  }
  return true;
}

} // namespace

int main() {
  constexpr std::array<std::byte, 0U> empty{};
  constexpr std::string_view abc = "abc";
  if (!equals_hex(gneiss::tooling::asset_build::sha256(empty),
                  "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")) {
    return 1;
  }
  const auto bytes = std::as_bytes(std::span{abc});
  return equals_hex(gneiss::tooling::asset_build::sha256(bytes),
                    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")
             ? 0
             : 2;
}
