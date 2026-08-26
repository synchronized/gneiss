// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_ASSET_HPP_
#define GNEISS_ASSET_HPP_

#include <gneiss/asset.h>
#include <gneiss/core/result.hpp>

#include <string_view>

namespace gneiss {

/** 校验规范形式的资产 URI；不访问文件系统。 */
[[nodiscard]] inline result validate_asset_uri(std::string_view uri) noexcept {
  return from_native(gneiss_asset_uri_validate(uri.data(), uri.size()));
}

} // namespace gneiss

#endif
