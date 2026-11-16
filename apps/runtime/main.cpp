// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/error.hpp>

#include "runtime_application.hpp"

int main(int argc, char** argv) {
  gneiss::install_fatal_handler();
  try {
    return gneiss::host::run_runtime_application(argc, argv);
  } catch (...) {
    gneiss::fatal_current_exception();
  }
}
