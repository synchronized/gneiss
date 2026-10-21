// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/gneiss.h>

#include <cstdio>

int main() {
  std::printf("gneiss %u.%u.%u\n", gneiss_version_major(), gneiss_version_minor(),
              gneiss_version_patch());
}
