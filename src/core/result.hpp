// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <gneiss/core/result.h>

namespace gneiss::core {
/** 静态结果文本，线程安全；未知值保持可诊断，不分配内存。 */
[[nodiscard]] const char* result_message(gneiss_result value) noexcept;
} // namespace gneiss::core
