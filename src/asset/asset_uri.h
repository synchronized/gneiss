// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_ASSET_ASSET_URI_H_
#define GNEISS_ASSET_ASSET_URI_H_

#include <gneiss/core/result.h>

#include <string_view>

namespace gneiss::asset_internal {

[[nodiscard]] gneiss_result validate_uri(std::string_view uri) noexcept;
[[nodiscard]] std::string_view uri_path(std::string_view uri) noexcept;

} // namespace gneiss::asset_internal

#endif
