// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/log.h>

int main(void) {
  gneiss_log_message message = GNEISS_LOG_MESSAGE_INIT;
  message.category = "game";
  message.category_length = UINT64_C(4);
  message.message = "ready";
  message.message_length = UINT64_C(5);
  if (GNEISS_LOG_MESSAGE_VERSION_1_SIZE != sizeof(gneiss_log_message) ||
      gneiss_log_message_validate(&message) != GNEISS_SUCCESS) {
    return 1;
  }
  message.severity = UINT32_C(0);
  if (gneiss_log_message_validate(&message) != GNEISS_ERROR_INVALID_ARGUMENT) {
    return 2;
  }
  message.severity = GNEISS_LOG_INFO;
  message.category = NULL;
  if (gneiss_log_message_validate(&message) != GNEISS_ERROR_INVALID_ARGUMENT ||
      gneiss_log_message_validate(NULL) != GNEISS_ERROR_INVALID_ARGUMENT) {
    return 3;
  }
  return 0;
}
