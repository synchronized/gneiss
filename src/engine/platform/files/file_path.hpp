// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/utf8.hpp"
#include <string_view>

namespace gneiss::engine::files::detail {
/** 原样路由 UTF-8 路径，不解码百分号；拒绝会被不同后端歧义解释的形式。 */
inline bool valid_file_path(std::string_view path) noexcept {
  if (!utf8::is_valid(path))
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
