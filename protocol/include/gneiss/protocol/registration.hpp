// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <gneiss/engine/core/base/error.hpp>

namespace gneiss::protocol {
/** 由宿主通过 initialize_meta 回调在冻结前注册；编解码不隐式注册。 */
[[nodiscard]] error register_types();
}
