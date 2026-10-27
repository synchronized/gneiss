// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <cstdint>
#include <string_view>

namespace gneiss::engine::files::detail {
[[nodiscard]] inline bool is_valid_utf8(std::string_view text) noexcept {
  std::size_t index = 0;
  while (index < text.size()) {
    const auto first = static_cast<unsigned char>(text[index]);
    if (first < 0x80U) {
      ++index;
      continue;
    }
    std::size_t count = 0;
    std::uint32_t value = 0;
    if (first >= 0xC2U && first <= 0xDFU) {
      count = 1;
      value = first & 0x1FU;
    } else if (first >= 0xE0U && first <= 0xEFU) {
      count = 2;
      value = first & 0x0FU;
    } else if (first >= 0xF0U && first <= 0xF4U) {
      count = 3;
      value = first & 0x07U;
    } else {
      return false;
    }
    if (index + count >= text.size()) {
      return false;
    }
    for (std::size_t offset = 1; offset <= count; ++offset) {
      const auto next = static_cast<unsigned char>(text[index + offset]);
      if ((next & 0xC0U) != 0x80U) {
        return false;
      }
      value = (value << 6U) | (next & 0x3FU);
    }
    if ((count == 2 && value < 0x800U) || (count == 3 && value < 0x10000U) || value > 0x10FFFFU ||
        (value >= 0xD800U && value <= 0xDFFFU)) {
      return false;
    }
    index += count + 1;
  }
  return true;
}

/** 原样路由 UTF-8 路径，不解码百分号；拒绝会被不同后端歧义解释的形式。 */
inline bool valid_file_path(std::string_view path) noexcept {
  if (!is_valid_utf8(path))
    return false;
  for (const auto ch : path) {
    const auto value = static_cast<unsigned char>(ch);
    if (value < 0x20U || value == 0x7FU)
      return false;
  }
  if (path.find_first_of("\\%?#:") != std::string_view::npos ||
      path.find('\0') != std::string_view::npos || path.starts_with('/'))
    return false;
  while (!path.empty()) {
    const auto slash = path.find('/');
    const auto part = path.substr(0, slash);
    if (part.empty() || part == "." || part == "..")
      return false;
    if (slash == std::string_view::npos)
      break;
    path.remove_prefix(slash + 1);
  }
  return true;
}
inline bool valid_file_uri(std::string_view uri) noexcept {
  const auto marker = uri.find("://");
  if (marker == std::string_view::npos || marker == 0)
    return false;
  for (const auto ch : uri.substr(0, marker)) {
    if (ch < 'a' || ch > 'z')
      return false;
  }
  return valid_file_path(uri.substr(marker + 3));
}
} // namespace gneiss::engine::files::detail
