// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/export.hpp"
#include <cstdint>

namespace gneiss::engine {
struct version {
  std::uint32_t major;
  std::uint32_t minor;
  std::uint32_t patch;
};
/** 返回本库构建版本，无状态且线程安全。 */
[[nodiscard]] GNEISS_API version library_version() noexcept;
/** 返回静态存储的 UTF-8 构建版本字符串，无需释放。 */
[[nodiscard]] GNEISS_API const char* library_version_string() noexcept;
} // namespace gneiss::engine
