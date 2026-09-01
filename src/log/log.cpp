// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/log.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>

namespace {

[[nodiscard]] bool is_valid_utf8(const char* value, std::uint64_t length) noexcept {
  if (length == 0U) {
    return true;
  }
  if (value == nullptr) {
    return false;
  }

  std::uint64_t index = 0U;
  while (index < length) {
    const auto lead = static_cast<unsigned char>(value[index]);
    std::uint32_t code_point = 0U;
    std::uint32_t continuation_count = 0U;
    if (lead <= 0x7FU) {
      ++index;
      continue;
    }
    if (lead >= 0xC2U && lead <= 0xDFU) {
      code_point = lead & 0x1FU;
      continuation_count = 1U;
    } else if (lead >= 0xE0U && lead <= 0xEFU) {
      code_point = lead & 0x0FU;
      continuation_count = 2U;
    } else if (lead >= 0xF0U && lead <= 0xF4U) {
      code_point = lead & 0x07U;
      continuation_count = 3U;
    } else {
      return false;
    }
    if (continuation_count > length - index - 1U) {
      return false;
    }
    for (std::uint32_t offset = 1U; offset <= continuation_count; ++offset) {
      const auto byte = static_cast<unsigned char>(value[index + offset]);
      if ((byte & 0xC0U) != 0x80U) {
        return false;
      }
      code_point = (code_point << 6U) | (byte & 0x3FU);
    }
    const auto minimum = continuation_count == 1U   ? 0x80U
                         : continuation_count == 2U ? 0x800U
                                                    : 0x10000U;
    if (code_point < minimum || code_point > 0x10FFFFU ||
        (code_point >= 0xD800U && code_point <= 0xDFFFU)) {
      return false;
    }
    index += continuation_count + 1U;
  }
  return true;
}

} // namespace

extern "C" gneiss_result gneiss_log_message_validate(const gneiss_log_message* message) {
  if (message == nullptr || message->struct_size < GNEISS_LOG_MESSAGE_VERSION_1_SIZE ||
      message->severity < GNEISS_LOG_TRACE || message->severity > GNEISS_LOG_FATAL ||
      message->category == nullptr || message->category_length == 0U || message->flags != 0U ||
      std::any_of(
          std::begin(message->reserved), std::end(message->reserved),
          [](std::uint64_t value) { return value != 0U; }) ||
      !is_valid_utf8(message->category, message->category_length) ||
      !is_valid_utf8(message->message, message->message_length)) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  return GNEISS_SUCCESS;
}
