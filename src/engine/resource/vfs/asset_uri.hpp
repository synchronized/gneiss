// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_ASSET_ASSET_URI_HPP_
#define GNEISS_ASSET_ASSET_URI_HPP_

#include "engine/core/result.hpp"

#include <string_view>

namespace gneiss::engine::resource {

[[nodiscard]] GNEISS_API gneiss::engine::core::result validate_uri(std::string_view uri) noexcept;
[[nodiscard]] std::string_view uri_path(std::string_view uri) noexcept;

} // namespace gneiss::engine::resource

#endif
