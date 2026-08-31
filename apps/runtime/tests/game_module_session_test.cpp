// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "game_module_session.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

void set_trace_path(const std::filesystem::path& path) {
#if defined(_WIN32)
  (void)_putenv_s("GNEISS_GAME_MODULE_TEST_TRACE", path.string().c_str());
#else
  (void)setenv("GNEISS_GAME_MODULE_TEST_TRACE", path.string().c_str(), 1);
#endif
}

std::string read_trace(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

} // namespace

int main(int argc, char** argv) {
  if (argc != 5) {
    return 1;
  }
  const std::filesystem::path valid_path = argv[1];
  const std::filesystem::path missing_symbol_path = argv[2];
  const std::filesystem::path invalid_abi_path = argv[3];
  const std::filesystem::path initialize_failure_path = argv[4];
  const auto trace_path = valid_path.parent_path() / "game-module-session.trace";
  std::error_code error;
  std::filesystem::remove(trace_path, error);
  set_trace_path(trace_path);

  gneiss::game_module_session session;
  if (session.load(valid_path) != gneiss::result::success || !session.is_loaded() ||
      session.module_id() != "gneiss.test.fixture" ||
      session.initialize(GNEISS_NULL_GAME_CONTEXT) != gneiss::result::invalid_state ||
      session.initialize(UINT64_C(1)) != gneiss::result::success) {
    return 2;
  }
  gneiss_game_update_time time{sizeof(time), 0, 1, 16'000'000, 16'000'000};
  if (session.fixed_update(time) != gneiss::result::success ||
      session.update(time) != gneiss::result::success) {
    return 3;
  }
  time.update_index = 99;
  if (session.update(time) != gneiss::result::internal ||
      session.shutdown() != gneiss::result::success ||
      session.shutdown() != gneiss::result::invalid_state || read_trace(trace_path) != "IFUUS") {
    return 4;
  }

  gneiss::game_module_session missing_symbol;
  gneiss::game_module_session invalid_abi;
  gneiss::game_module_session initialize_failure;
  if (missing_symbol.load(missing_symbol_path) != gneiss::result::not_found ||
      invalid_abi.load(invalid_abi_path) != gneiss::result::invalid_argument ||
      initialize_failure.load(initialize_failure_path) != gneiss::result::success ||
      initialize_failure.initialize(UINT64_C(1)) != gneiss::result::initialization_failed) {
    return 5;
  }
  if (read_trace(trace_path) != "IFUUSI") {
    return 6;
  }
  {
    gneiss::game_module_session early_exit;
    if (early_exit.load(valid_path) != gneiss::result::success ||
        early_exit.initialize(UINT64_C(2)) != gneiss::result::success) {
      return 7;
    }
  }
  gneiss::game_module_session missing_file;
  if (missing_file.load(valid_path.parent_path() / "missing-game-module.dll") !=
          gneiss::result::not_found ||
      read_trace(trace_path) != "IFUUSIIS") {
    return 8;
  }
  return 0;
}
