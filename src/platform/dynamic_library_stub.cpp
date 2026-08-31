// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "dynamic_library.h"

namespace gneiss {

dynamic_library::~dynamic_library() = default;
result dynamic_library::open(const std::filesystem::path&) noexcept { return result::unsupported; }
result dynamic_library::find_symbol(const char*, void**) const noexcept {
  return result::unsupported;
}
void dynamic_library::close() noexcept {}
bool dynamic_library::is_open() const noexcept { return false; }

} // namespace gneiss
