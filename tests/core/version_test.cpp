// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/core/version.hpp>

#include <cstdio>

int main() {
  const auto version = gneiss::library_version();
  std::printf("gneiss %u.%u.%u\n", version.major, version.minor, version.patch);
}
