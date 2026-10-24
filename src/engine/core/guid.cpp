// SPDX-License-Identifier: MIT
// Copyright (c) 2026 CAORS contributors
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/guid.hpp"

#include <cstddef>
#include <string>
#include <string_view>

namespace gneiss::engine {
namespace {

[[nodiscard]] constexpr int hex_digit_value(char digit) noexcept {
  if (digit >= '0' && digit <= '9') {
    return digit - '0';
  }

  if (digit >= 'a' && digit <= 'f') {
    return digit - 'a' + 10;
  }

  if (digit >= 'A' && digit <= 'F') {
    return digit - 'A' + 10;
  }

  return -1;
}

[[nodiscard]] constexpr char hex_digit(std::uint8_t value) noexcept {
  constexpr char digits[] = "0123456789abcdef";
  return digits[value & 0x0fU];
}

[[nodiscard]] constexpr bool is_dash_position(std::size_t index) noexcept {
  return index == 8 || index == 13 || index == 18 || index == 23;
}

} // namespace

std::optional<guid> guid::parse(std::string_view text) {
  if (text.size() != 36) {
    return std::nullopt;
  }

  byte_array bytes{};
  std::size_t byte_index = 0;
  int high_nibble = -1;

  for (std::size_t index = 0; index < text.size(); ++index) {
    const char current = text[index];
    if (is_dash_position(index)) {
      if (current != '-') {
        return std::nullopt;
      }

      continue;
    }

    const int value = hex_digit_value(current);
    if (value < 0) {
      return std::nullopt;
    }

    if (high_nibble < 0) {
      high_nibble = value;
      continue;
    }

    if (byte_index >= bytes.size()) {
      return std::nullopt;
    }

    bytes[byte_index++] = static_cast<std::uint8_t>((high_nibble << 4) | value);
    high_nibble = -1;
  }

  if (byte_index != bytes.size() || high_nibble >= 0) {
    return std::nullopt;
  }

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
