// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "dynamic_library.hpp"
#include "engine/core/error.hpp"

#include <dlfcn.h>

namespace gneiss::engine {

dynamic_library::~dynamic_library() { close(); }

error dynamic_library::open(const std::filesystem::path& path) noexcept {
  if (handle_ != nullptr || path.empty()) {
    return error::invalid_state;
  }
  std::error_code error;
  if (!std::filesystem::is_regular_file(path, error)) {
    return error::not_found;
  }
  handle_ = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  return handle_ != nullptr ? error::success : error::dependency_failed;
}

error dynamic_library::find_symbol(const char* name, void** out_symbol) const noexcept {
  if (handle_ == nullptr || name == nullptr || name[0] == '\0' || out_symbol == nullptr) {
    return error::invalid_argument;
  }
  dlerror();
  *out_symbol = dlsym(handle_, name);
  return dlerror() == nullptr && *out_symbol != nullptr ? error::success : error::not_found;
}

void dynamic_library::close() noexcept {
  if (handle_ != nullptr) {
    dlclose(handle_);
    handle_ = nullptr;
  }
}

bool dynamic_library::is_open() const noexcept { return handle_ != nullptr; }

} // namespace gneiss::engine
