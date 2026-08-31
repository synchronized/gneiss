// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/game_module.hpp>

#include <type_traits>

int main() {
  static_assert(gneiss::game_module_abi_version == GNEISS_GAME_MODULE_ABI_VERSION_1);
  static_assert(gneiss::game_module_query_symbol == GNEISS_GAME_MODULE_QUERY_SYMBOL);
  static_assert(std::is_trivially_copyable_v<gneiss::game_context>);

  const gneiss::game_context context{42U};
  if (!context.is_valid() || context.get() != 42U || gneiss::null_game_context.is_valid()) {
    return 1;
  }

  gneiss_game_module_desc desc{};
  if (gneiss::validate_game_module(desc) != gneiss::result::invalid_argument) {
    return 2;
  }
  return 0;
}
