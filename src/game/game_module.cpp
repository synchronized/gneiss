// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/game_module.h>

extern "C" gneiss_result gneiss_game_module_validate(const gneiss_game_module_desc* desc) {
  if (desc == nullptr || desc->struct_size < GNEISS_GAME_MODULE_DESC_VERSION_1_SIZE ||
      desc->abi_version != GNEISS_GAME_MODULE_ABI_VERSION_CURRENT || desc->module_id == nullptr ||
      desc->module_id_length == 0U || desc->initialize == nullptr ||
      desc->fixed_update == nullptr || desc->update == nullptr || desc->shutdown == nullptr ||
      desc->reserved[0] != 0U || desc->reserved[1] != 0U) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  return GNEISS_SUCCESS;
}
