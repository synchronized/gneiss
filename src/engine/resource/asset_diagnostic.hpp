// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/result.hpp"
#include <cstddef>
#include <string>
#include <string_view>

namespace gneiss {
struct asset_diagnostic final {
  gneiss::error error = gneiss::error::success;
  std::size_t byte_offset = 0;
  std::string path{};
  std::string message{};
};

}

namespace gneiss::detail {
inline void fail(asset_diagnostic& diagnostic, gneiss::error result, std::string_view path,
                 std::string_view message, std::size_t offset = 0) {
  diagnostic.error = result;
  diagnostic.byte_offset = offset;
  {
    diagnostic.path = path;
    diagnostic.message = message;
  }
}
}
