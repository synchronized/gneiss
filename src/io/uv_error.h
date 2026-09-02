// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SRC_IO_UV_ERROR_H_
#define GNEISS_SRC_IO_UV_ERROR_H_

#include <gneiss/core/result.hpp>

namespace gneiss {

/** 将 libuv 返回值转换为稳定的 Gneiss 结果码，不暴露 libuv 类型。 */
[[nodiscard]] result from_uv_error(int value) noexcept;

} // namespace gneiss

#endif
