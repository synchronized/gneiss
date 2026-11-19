// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/core/base/error.hpp>

#include "assetc_application.hpp"

int main(int argc, char** argv) {
  gneiss::install_fatal_handler();
  try {
    return gneiss::host::run(argc, argv);
  } catch (...) {
    gneiss::fatal_current_exception();
  }
}
