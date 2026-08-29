// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "gneiss_abi.h"

#include <string.h>

typedef struct baseline_state {
  uint32_t updates;
  uint32_t shutdowns;
} baseline_state;

static gneiss_result update(gneiss_application application, const gneiss_frame_time* time,
                            void* user_data) {
  baseline_state* state = (baseline_state*)user_data;
  if (application == GNEISS_NULL_APPLICATION || time == NULL || state == NULL) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  state->updates += UINT32_C(1);
  return gneiss_application_request_exit(application);
}

static void shutdown(void* user_data) {
  baseline_state* state = (baseline_state*)user_data;
  if (state != NULL) {
    state->shutdowns += UINT32_C(1);
  }
}

int main(void) {
  baseline_state state = {0};
  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  gneiss_application application = GNEISS_NULL_APPLICATION;
  desc.user_data = &state;
  desc.update = update;
  desc.shutdown = shutdown;

  if (gneiss_version_major() > UINT32_C(1) ||
      strcmp(gneiss_result_message(GNEISS_SUCCESS), "success") != 0 ||
      gneiss_application_create(&desc, &application) != GNEISS_SUCCESS ||
      application == GNEISS_NULL_APPLICATION ||
      gneiss_application_run(application, UINT64_C(2)) != GNEISS_SUCCESS ||
      state.updates != UINT32_C(1) || gneiss_application_destroy(application) != GNEISS_SUCCESS ||
      state.shutdowns != UINT32_C(1)) {
    return 1;
  }
  return 0;
}
