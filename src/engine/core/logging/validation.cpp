// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/logging/validation.hpp"
#include "engine/core/logging/record.hpp"

#include "engine/core/utf8.hpp"

bool gneiss::engine::logging::valid_text(std::string_view category,
                                         std::string_view message) noexcept {
  return !category.empty() && utf8::is_valid(category) && utf8::is_valid(message);
}

gneiss::engine::error
gneiss::engine::logging::validate_message(const message_view& message) noexcept {
  return message.severity >= level::trace && message.severity <= level::fatal &&
                 valid_text(message.category, message.message)
             ? error::success
             : error::invalid_argument;
}
