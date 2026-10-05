// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/api/log_validation.hpp"

extern "C" gneiss_result gneiss_log_message_validate(const gneiss_log_message* message) {
  return gneiss::abi_internal::validate_log_message(message);
}
