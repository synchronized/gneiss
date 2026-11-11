// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_ENGINE_LOGGING_VALIDATION_HPP
#define GNEISS_ENGINE_LOGGING_VALIDATION_HPP

#include "engine/core/error.hpp"
#include <string_view>

namespace gneiss::engine::logging {
struct message_view;
/** 校验原生日志级别及 UTF-8 文本，不分配、不取得文本所有权。 */
[[nodiscard]] error validate_message(const message_view& message) noexcept;
/** 消息视图只在调用期间借用；类别非空，文本须为合法 UTF-8，无分配且线程安全。 */
[[nodiscard]] bool valid_text(std::string_view category, std::string_view message) noexcept;
} // namespace gneiss::engine::logging

#endif
