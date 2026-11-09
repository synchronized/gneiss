// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/fatal.hpp"

#include "project_cli.hpp"

int main(int argc, char** argv) {
  gneiss::engine::install_fatal_handler();
  try {
    return gneiss::host::run_project_cli(argc, argv);
  } catch (...) {
    gneiss::engine::fatal_current_exception();
  }
}
