// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <cstdint>

namespace gneiss::core {
struct version {
  std::uint32_t major;
  std::uint32_t minor;
  std::uint32_t patch;
};
/** 返回本库构建版本，无状态且线程安全。 */
[[nodiscard]] version library_version() noexcept;
} // namespace gneiss::core
