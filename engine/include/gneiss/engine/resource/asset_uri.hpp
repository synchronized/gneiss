// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_ASSET_ASSET_URI_HPP_
#define GNEISS_ASSET_ASSET_URI_HPP_

#include <gneiss/engine/resource/export.hpp>
#include <gneiss/engine/core/base/error.hpp>

#include <string_view>

namespace gneiss {

[[nodiscard]] GNEISS_RESOURCE_API error validate_uri(std::string_view uri) noexcept;
[[nodiscard]] std::string_view uri_path(std::string_view uri) noexcept;

} // namespace gneiss

#endif
