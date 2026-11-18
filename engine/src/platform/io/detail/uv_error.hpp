// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include <gneiss/engine/platform/io/export.hpp>

#include <gneiss/engine/error.hpp>

namespace gneiss::io::detail {

/** 将 libuv 返回值转换为稳定的 Gneiss 结果码，不暴露 libuv 类型。 */
[[nodiscard]] GNEISS_IO_API error from_uv_status(int value) noexcept;

} // namespace gneiss::io::detail
