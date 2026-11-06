// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "dynamic_library.hpp"
#include "engine/core/error.hpp"

namespace gneiss::engine {

dynamic_library::~dynamic_library() = default;
error dynamic_library::open(const std::filesystem::path&) noexcept { return error::unsupported; }
error dynamic_library::find_symbol(const char*, void**) const noexcept {
  return error::unsupported;
}
void dynamic_library::close() noexcept {}
bool dynamic_library::is_open() const noexcept { return false; }

} // namespace gneiss::engine
