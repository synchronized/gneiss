// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/resource/vfs/asset_uri.hpp"
#include "engine/core/result.hpp"
#include "engine/platform/files/file_path.hpp"

namespace {

constexpr std::string_view scheme = "asset://";

} // namespace

namespace gneiss::engine {

gneiss::engine::result validate_uri(std::string_view uri) noexcept {
  if (!uri.starts_with(scheme) || uri.size() == scheme.size() || uri.ends_with('/') ||
      !detail::valid_file_path(uri.substr(scheme.size()))) {
    return gneiss::engine::result::invalid_argument;
  }
  return gneiss::engine::result::success;
}

std::string_view uri_path(std::string_view uri) noexcept { return uri.substr(scheme.size()); }

} // namespace gneiss::engine
