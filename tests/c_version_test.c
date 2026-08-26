// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/gneiss.h>

int main(void) {
  return gneiss_version_major() == GNEISS_VERSION_MAJOR &&
                 gneiss_version_minor() == GNEISS_VERSION_MINOR &&
                 gneiss_version_patch() == GNEISS_VERSION_PATCH
             ? 0
             : 1;
}
