// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/api/result_conversion.hpp"
#include "engine/core/result.hpp"
#include "engine/resource/vfs/asset_uri.hpp"
#include <cstddef>
#include <gneiss/engine/asset.h>
#include <limits>

extern "C" gneiss_result gneiss_asset_uri_validate(const char* uri, uint64_t uri_length) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    if (uri == nullptr || uri_length > std::numeric_limits<std::size_t>::max()) {
      return gneiss::engine::result::invalid_argument;
    }
    return gneiss::engine::validate_uri(
        std::string_view(uri, static_cast<std::size_t>(uri_length)));
  }());
}
