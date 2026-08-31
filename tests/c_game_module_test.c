// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/game_module.h>

#include <stddef.h>
#include <stdint.h>

static gneiss_result initialize(gneiss_game_context context, void** out_module_state) {
  static uint32_t state = UINT32_C(7);
  if (context == GNEISS_NULL_GAME_CONTEXT || out_module_state == NULL) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_module_state = &state;
  return GNEISS_SUCCESS;
}

static gneiss_result fixed_update(gneiss_game_context context, void* module_state,
                                  const gneiss_game_update_time* time) {
  return context != GNEISS_NULL_GAME_CONTEXT && module_state != NULL && time != NULL
             ? GNEISS_SUCCESS
             : GNEISS_ERROR_INVALID_ARGUMENT;
}

static gneiss_result update(gneiss_game_context context, void* module_state,
                            const gneiss_game_update_time* time) {
  return fixed_update(context, module_state, time);
}

static gneiss_result shutdown(gneiss_game_context context, void* module_state) {
  return context != GNEISS_NULL_GAME_CONTEXT && module_state != NULL
             ? GNEISS_SUCCESS
             : GNEISS_ERROR_INVALID_ARGUMENT;
}

static gneiss_game_module_desc valid_desc(void) {
  gneiss_game_module_desc desc = {0};
  desc.struct_size = GNEISS_GAME_MODULE_DESC_VERSION_1_SIZE;
  desc.abi_version = GNEISS_GAME_MODULE_ABI_VERSION_CURRENT;
  desc.module_id = "gneiss.tests.module";
  desc.module_id_length = UINT64_C(19);
  desc.initialize = initialize;
  desc.fixed_update = fixed_update;
  desc.update = update;
  desc.shutdown = shutdown;
  return desc;
}

int main(void) {
  gneiss_game_module_desc desc = valid_desc();
  if (gneiss_game_module_validate(&desc) != GNEISS_SUCCESS ||
      GNEISS_GAME_MODULE_DESC_VERSION_1_SIZE != sizeof(gneiss_game_module_desc) ||
      GNEISS_GAME_UPDATE_TIME_VERSION_1_SIZE != sizeof(gneiss_game_update_time)) {
    return 1;
  }

  desc.struct_size = GNEISS_GAME_MODULE_DESC_VERSION_1_SIZE - UINT32_C(1);
  if (gneiss_game_module_validate(&desc) != GNEISS_ERROR_INVALID_ARGUMENT) {
    return 2;
  }
  desc = valid_desc();
  desc.abi_version = GNEISS_GAME_MODULE_ABI_VERSION_CURRENT + UINT32_C(1);
  if (gneiss_game_module_validate(&desc) != GNEISS_ERROR_INVALID_ARGUMENT) {
    return 3;
  }
  desc = valid_desc();
  desc.module_id = NULL;
  if (gneiss_game_module_validate(&desc) != GNEISS_ERROR_INVALID_ARGUMENT) {
    return 4;
  }
  desc = valid_desc();
  desc.update = NULL;
  if (gneiss_game_module_validate(&desc) != GNEISS_ERROR_INVALID_ARGUMENT) {
    return 5;
  }
  desc = valid_desc();
  desc.reserved[0] = UINT64_C(1);
  if (gneiss_game_module_validate(&desc) != GNEISS_ERROR_INVALID_ARGUMENT ||
      gneiss_game_module_validate(NULL) != GNEISS_ERROR_INVALID_ARGUMENT) {
    return 6;
  }
  return 0;
}
