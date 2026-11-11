// SPDX-License-Identifier: MIT
// Copyright (c) 2026 CAORS contributors
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/guid.hpp"

#include <cstddef>
#include <random>
#include <string>
#include <string_view>

namespace gneiss {
namespace {

[[nodiscard]] constexpr char hex_digit(std::uint8_t value) noexcept {
  constexpr char digits[] = "0123456789abcdef";
  return digits[value & 0x0fU];
}

} // namespace

guid guid::generate() {
  byte_array bytes{};
  std::random_device random;
  for (auto& byte : bytes)
    byte = static_cast<std::uint8_t>(random());
  bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0fU) | 0x40U);
  bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3fU) | 0x80U);
  return guid{bytes};
}

bool guid::is_nil() const noexcept {
  for (const std::uint8_t byte : bytes_) {
    if (byte != 0) {
      return false;
    }
  }

  return true;
}

std::string guid::string() const {
  std::string result;
  result.reserve(36);

  for (std::size_t index = 0; index < bytes_.size(); ++index) {
    if (index == 4 || index == 6 || index == 8 || index == 10) {
      result.push_back('-');
    }

    const std::uint8_t byte = bytes_[index];
    result.push_back(hex_digit(static_cast<std::uint8_t>(byte >> 4)));
    result.push_back(hex_digit(byte));
  }

  return result;
}

std::size_t guid_hash::operator()(const guid& value) const noexcept {
  std::uint64_t hash = 1469598103934665603ULL;
  for (const std::uint8_t byte : value.bytes()) {
    hash ^= byte;
    hash *= 1099511628211ULL;
  }

  // 先固定 64 位混合，再折叠到目标位宽，避免 wasm32 隐式截断。
  if constexpr (sizeof(std::size_t) < sizeof(std::uint64_t)) {
    return static_cast<std::size_t>(hash ^ (hash >> 32U));
  }
  return static_cast<std::size_t>(hash);
}

}
