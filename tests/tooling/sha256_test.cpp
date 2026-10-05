// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "tooling/asset_build/sha256.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace {

[[nodiscard]] bool equals_hex(const gneiss::tooling::asset_build::sha256_digest& digest,
                              std::string_view expected) {
  constexpr std::string_view digits = "0123456789abcdef";
  for (std::size_t index = 0U; index < digest.size(); ++index) {
    const auto value = std::to_integer<unsigned char>(digest[index]);
    if (expected[index * 2U] != digits[value >> 4U] ||
        expected[(index * 2U) + 1U] != digits[value & 0x0fU]) {
      return false;
    }
  }
  return true;
}

} // namespace

int main() try {
  constexpr std::array<std::byte, 0U> empty{};
  constexpr std::string_view abc = "abc";
  if (!equals_hex(gneiss::tooling::asset_build::sha256(empty),
                  "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")) {
    return 1;
  }
  const auto bytes = std::as_bytes(std::span{abc});
  if (!equals_hex(gneiss::tooling::asset_build::sha256(bytes),
                  "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")) {
    return 2;
  }
  {
    const std::string input(55U, 'a');
    if (!equals_hex(gneiss::tooling::asset_build::sha256(std::as_bytes(std::span{input})),
                    "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318")) {
      return 3;
    }
  }
  {
    const std::string input(56U, 'a');
    if (!equals_hex(gneiss::tooling::asset_build::sha256(std::as_bytes(std::span{input})),
                    "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a")) {
      return 4;
    }
  }
  {
    const std::string input(63U, 'a');
    if (!equals_hex(gneiss::tooling::asset_build::sha256(std::as_bytes(std::span{input})),
                    "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34")) {
      return 5;
    }
  }
  {
    const std::string input(64U, 'a');
    if (!equals_hex(gneiss::tooling::asset_build::sha256(std::as_bytes(std::span{input})),
                    "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb")) {
      return 6;
    }
  }
  {
    const std::string input(65U, 'a');
    if (!equals_hex(gneiss::tooling::asset_build::sha256(std::as_bytes(std::span{input})),
                    "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0")) {
      return 7;
    }
  }
  {
    const std::string input(119U, 'a');
    if (!equals_hex(gneiss::tooling::asset_build::sha256(std::as_bytes(std::span{input})),
                    "31eba51c313a5c08226adf18d4a359cfdfd8d2e816b13f4af952f7ea6584dcfb")) {
      return 8;
    }
  }
  {
    const std::string input(120U, 'a');
    if (!equals_hex(gneiss::tooling::asset_build::sha256(std::as_bytes(std::span{input})),
                    "2f3d335432c70b580af0e8e1b3674a7c020d683aa5f73aaaedfdc55af904c21c")) {
      return 9;
    }
  }
  {
    const std::string input(127U, 'a');
    if (!equals_hex(gneiss::tooling::asset_build::sha256(std::as_bytes(std::span{input})),
                    "c57e9278af78fa3cab38667bef4ce29d783787a2f731d4e12200270f0c32320a")) {
      return 10;
    }
  }
  {
    const std::string input(128U, 'a');
    if (!equals_hex(gneiss::tooling::asset_build::sha256(std::as_bytes(std::span{input})),
                    "6836cf13bac400e9105071cd6af47084dfacad4e5e302c94bfed24e013afb73e")) {
      return 11;
    }
  }
  {
    const std::string input(1000000U, 'a');
    if (!equals_hex(gneiss::tooling::asset_build::sha256(std::as_bytes(std::span{input})),
                    "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0")) {
      return 12;
    }
  }
  // 每个分割位置验证尾块与填充边界；digest 可重复调用并继续追加。
  for (const auto length : {0U, 1U, 55U, 56U, 63U, 64U, 65U, 119U, 120U, 128U, 257U}) {
    const std::string input(length, 'a');
    const auto data = std::as_bytes(std::span{input});
    const auto expected = gneiss::core::sha256(data);
    for (std::size_t split = 0U; split <= data.size(); ++split) {
      gneiss::core::sha256_builder builder;
      builder.update(data.first(split));
      if (builder.digest() != gneiss::core::sha256(data.first(split))) {
        return 13;
      }
      builder.update({});
      builder.update(data.subspan(split));
      if (builder.digest() != expected || builder.digest() != expected) {
        return 14;
      }
    }
  }
  gneiss::core::sha256_builder million;
  const std::string block(1000U, 'a');
  for (int index = 0; index < 1000; ++index) {
    million.update(std::as_bytes(std::span{block}));
  }
  if (!equals_hex(million.digest(),
                  "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0")) {
    return 15;
  }
  return 0;
} catch (...) {
  return 16;
}
