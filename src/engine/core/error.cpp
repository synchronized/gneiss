// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/error.hpp"

const char* gneiss::engine::error_message(error value) noexcept {
  switch (value.code()) {
  case error::success.code():
    return "success";
  case error::unknown.code():
    return "unknown error";
  case error::invalid_argument.code():
    return "invalid argument";
  case error::invalid_handle.code():
    return "invalid handle";
  case error::out_of_memory.code():
    return "out of memory";
  case error::unsupported.code():
    return "unsupported operation";
  case error::initialization_failed.code():
    return "initialization failed";
  case error::dependency_failed.code():
    return "dependency failed";
  case error::invalid_state.code():
    return "invalid state";
  case error::not_ready.code():
    return "operation temporarily not ready";
  case error::internal.code():
    return "internal error";
  case error::not_found.code():
    return "not found";
  case error::io.code():
    return "I/O error";
  case error::cancelled.code():
    return "operation cancelled";
  default:
    return "unrecognized result";
  }
}
