// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/gneiss.hpp>

int main() {
  return gneiss::library_version() == gneiss::header_version &&
                 !gneiss::header_version_string.empty()
             ? 0
             : 1;
}
