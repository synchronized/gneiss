// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

#include <cstdint>

namespace gneiss::window {
/** 进程内窗口身份，不保活、不持久化；非零仅表示已分配，不保证窗口仍存活。 */
struct id {
  std::uint64_t value{};
  explicit constexpr operator bool() const noexcept { return value != 0; }
  friend constexpr bool operator==(id, id) noexcept = default;
};
} // namespace gneiss::window
