// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "asset/asset_uri.hpp"
#include <cstddef>
#include <gneiss/asset.h>
#include <limits>

extern "C" gneiss_result gneiss_asset_uri_validate(const char* uri, uint64_t uri_length) {
  if (uri == nullptr || uri_length > std::numeric_limits<std::size_t>::max()) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  return gneiss::asset_internal::validate_uri(
      std::string_view(uri, static_cast<std::size_t>(uri_length)));
}
