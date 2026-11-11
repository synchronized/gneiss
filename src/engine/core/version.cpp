// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/version.hpp"

#include "engine/core/build_version.h"

gneiss::version gneiss::library_version() noexcept {
  return {GNEISS_VERSION_MAJOR, GNEISS_VERSION_MINOR, GNEISS_VERSION_PATCH};
}

const char* gneiss::library_version_string() noexcept { return GNEISS_VERSION_STRING; }
