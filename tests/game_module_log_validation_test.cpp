// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/api/log_validation.hpp"

using gneiss::engine::result;
using gneiss::engine::detail::validate_log_message;

int main() {
  gneiss_log_message message = GNEISS_LOG_MESSAGE_INIT;
  message.category = "game";
  message.category_length = UINT64_C(4);
  message.message = "ready";
  message.message_length = UINT64_C(5);
  if (GNEISS_LOG_MESSAGE_VERSION_1_SIZE != sizeof(gneiss_log_message) ||
      validate_log_message(&message) != result::success) {
    return 1;
  }
  message.severity = UINT32_C(0);
  if (validate_log_message(&message) != result::invalid_argument) {
    return 2;
  }
  message.severity = GNEISS_LOG_INFO;
  message.category = nullptr;
  if (validate_log_message(&message) != result::invalid_argument ||
      validate_log_message(nullptr) != result::invalid_argument) {
    return 3;
  }
  message.category = "game";
  message.message = nullptr;
  if (validate_log_message(&message) != result::invalid_argument) {
    return 4;
  }
  message.message_length = 0;
  if (validate_log_message(&message) != result::success) {
    return 5;
  }
  message.message = "\xC0\x80";
  message.message_length = 2;
  if (validate_log_message(&message) != result::invalid_argument) {
    return 6;
  }
  message.message = "\xE4\xB8\xAD";
  message.message_length = 3;
  if (validate_log_message(&message) != result::success) {
    return 7;
  }
  message.reserved[0] = 1;
  if (validate_log_message(&message) != result::invalid_argument) {
    return 8;
  }
  message.reserved[0] = 0;
  message.struct_size = GNEISS_LOG_MESSAGE_VERSION_1_SIZE - 1;
  return validate_log_message(&message) == result::invalid_argument ? 0 : 9;
}
