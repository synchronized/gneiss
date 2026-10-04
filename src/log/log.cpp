// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "log/log.hpp"

#include <cstdint>

namespace {
[[nodiscard]] bool is_valid_utf8(std::string_view text) noexcept {
  const auto* value = text.data();
  const auto length = text.size();
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

bool gneiss::log_internal::valid_text(std::string_view category,
                                      std::string_view message) noexcept {
  return !category.empty() && is_valid_utf8(category) && is_valid_utf8(message);
}
