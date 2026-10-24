// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "dynamic_library.hpp"
#include "engine/core/result.hpp"

namespace gneiss::engine::platform {

dynamic_library::~dynamic_library() = default;
gneiss::engine::core::result dynamic_library::open(const std::filesystem::path&) noexcept {
  return gneiss::engine::core::result::unsupported;
}
gneiss::engine::core::result dynamic_library::find_symbol(const char*, void**) const noexcept {
  return gneiss::engine::core::result::unsupported;
}
void dynamic_library::close() noexcept {}
bool dynamic_library::is_open() const noexcept { return false; }

} // namespace gneiss::engine::platform
