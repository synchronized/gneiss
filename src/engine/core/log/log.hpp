// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SRC_LOG_LOG_HPP
#define GNEISS_SRC_LOG_LOG_HPP

#include <string_view>

namespace gneiss::log_internal {
/** 消息视图只在调用期间借用；类别非空，文本须为合法 UTF-8，无分配且线程安全。 */
[[nodiscard]] bool valid_text(std::string_view category, std::string_view message) noexcept;
} // namespace gneiss::log_internal

#endif
