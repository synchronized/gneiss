// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

#include "engine/core/result.hpp"
#include <gneiss/engine/core/result.h>

namespace gneiss {
// 迁移期值转换仅借用原生结果类型，不再维护第二份状态、常量或消息实现。
using engine::result;
[[nodiscard]] constexpr gneiss_result to_native(engine::result value) noexcept {
  return value.code();
}
[[nodiscard]] constexpr engine::result from_native(gneiss_result value) noexcept {
  return engine::result{value};
}
}
