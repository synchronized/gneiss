// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include <cstdint>

namespace gneiss::input {
/** 只在所属输入服务及映射代次内有效；零值无效。 */
struct action_id final {
  std::uint64_t value{};
  friend bool operator==(action_id, action_id) = default;
};

struct action_state final {
  bool pressed{};
  bool held{};
  bool released{};
  float value{};
};

} // namespace gneiss::input
