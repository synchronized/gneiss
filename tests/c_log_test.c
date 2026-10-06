// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/log.h>

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
  message.category = "game";
  message.message = NULL;
  if (gneiss_log_message_validate(&message) != GNEISS_ERROR_INVALID_ARGUMENT) {
    return 4;
  }
  message.message_length = 0;
  if (gneiss_log_message_validate(&message) != GNEISS_SUCCESS) {
    return 5;
  }
  message.message = "\xC0\x80";
  message.message_length = 2;
  if (gneiss_log_message_validate(&message) != GNEISS_ERROR_INVALID_ARGUMENT) {
    return 6;
  }
  message.message = "\xE4\xB8\xAD";
  message.message_length = 3;
  if (gneiss_log_message_validate(&message) != GNEISS_SUCCESS) {
    return 7;
  }
  message.reserved[0] = 1;
  if (gneiss_log_message_validate(&message) != GNEISS_ERROR_INVALID_ARGUMENT) {
    return 8;
  }
  message.reserved[0] = 0;
  message.struct_size = GNEISS_LOG_MESSAGE_VERSION_1_SIZE - 1;
  return gneiss_log_message_validate(&message) == GNEISS_ERROR_INVALID_ARGUMENT ? 0 : 9;
}
