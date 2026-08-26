// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "asset/asset_uri.h"

#include <gneiss/asset.h>

#include <cstddef>
#include <cstdint>
#include <limits>

namespace {

constexpr std::string_view scheme = "asset://";

[[nodiscard]] bool is_valid_utf8(std::string_view text) noexcept {
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

} // namespace

namespace gneiss::asset_internal {

gneiss_result validate_uri(std::string_view uri) noexcept {
  if (!uri.starts_with(scheme) || uri.size() == scheme.size() || !is_valid_utf8(uri)) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  const auto path = uri.substr(scheme.size());
  std::size_t segment_start = 0;
  for (std::size_t index = 0; index <= path.size(); ++index) {
    if (index != path.size() && path[index] != '/') {
      const auto value = static_cast<unsigned char>(path[index]);
      if (value < 0x20U || value == 0x7FU || path[index] == '\\' || path[index] == ':' ||
          path[index] == '?' || path[index] == '#' || path[index] == '%') {
        return GNEISS_ERROR_INVALID_ARGUMENT;
      }
      continue;
    }
    const auto segment = path.substr(segment_start, index - segment_start);
    if (segment.empty() || segment == "." || segment == "..") {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    segment_start = index + 1;
  }
  return GNEISS_SUCCESS;
}

std::string_view uri_path(std::string_view uri) noexcept { return uri.substr(scheme.size()); }

} // namespace gneiss::asset_internal

extern "C" gneiss_result gneiss_asset_uri_validate(const char* uri, uint64_t uri_length) {
  if (uri == nullptr || uri_length > std::numeric_limits<std::size_t>::max()) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  return gneiss::asset_internal::validate_uri(
      std::string_view(uri, static_cast<std::size_t>(uri_length)));
}
