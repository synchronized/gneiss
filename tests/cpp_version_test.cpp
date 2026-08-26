// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/gneiss.hpp>

int main() {
  const auto version = gneiss::library_version();
  return version.major == GNEISS_VERSION_MAJOR && version.minor == GNEISS_VERSION_MINOR &&
                 version.patch == GNEISS_VERSION_PATCH
             ? 0
             : 1;
}
