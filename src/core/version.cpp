// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "core/version.hpp"

#include <gneiss/core/version.h>

gneiss::core::version gneiss::core::library_version() noexcept {
  return {GNEISS_VERSION_MAJOR, GNEISS_VERSION_MINOR, GNEISS_VERSION_PATCH};
}
