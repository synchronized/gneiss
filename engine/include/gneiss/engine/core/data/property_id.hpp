// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

#include <cstdint>

namespace gneiss {
/** 类型内部的字段身份，零无效。 */
class property_id final {
public:
  constexpr property_id() noexcept = default;
  explicit constexpr property_id(std::uint32_t value) noexcept : value_(value) {}
  [[nodiscard]] constexpr std::uint32_t get() const { return value_; }
  [[nodiscard]] constexpr bool is_valid() const noexcept { return value_ != 0; }
  friend constexpr bool operator==(property_id, property_id) noexcept = default;

private:
  std::uint32_t value_ = 0;
};
inline constexpr property_id null_property_id{};
}
