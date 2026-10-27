// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/result.hpp"
#include <cstddef>
#include <string>
#include <string_view>

namespace gneiss::engine {
struct asset_diagnostic final {
  gneiss::engine::result result = gneiss::engine::result::success;
  std::size_t byte_offset = 0;
  std::string path;
  std::string message;
};

}

namespace gneiss::engine::detail {
inline void fail(asset_diagnostic& diagnostic, gneiss::engine::result result, std::string_view path,
                 std::string_view message, std::size_t offset = 0) noexcept {
  diagnostic.result = result;
  diagnostic.byte_offset = offset;
  try {
    diagnostic.path = path;
    diagnostic.message = message;
  } catch (...) {
    diagnostic.result = gneiss::engine::result::out_of_memory;
    diagnostic.path.clear();
    diagnostic.message.clear();
  }
}

}
