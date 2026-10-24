// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"

const char* gneiss::engine::result_message(result value) noexcept {
  switch (value.code()) {
  case result::success.code():
    return "success";
  case result::unknown.code():
    return "unknown error";
  case result::invalid_argument.code():
    return "invalid argument";
  case result::invalid_handle.code():
    return "invalid handle";
  case result::out_of_memory.code():
    return "out of memory";
  case result::unsupported.code():
    return "unsupported operation";
  case result::initialization_failed.code():
    return "initialization failed";
  case result::dependency_failed.code():
    return "dependency failed";
  case result::invalid_state.code():
    return "invalid state";
  case result::not_ready.code():
    return "operation temporarily not ready";
  case result::internal.code():
    return "internal error";
  case result::not_found.code():
    return "not found";
  case result::io.code():
    return "I/O error";
  default:
    return "unrecognized result";
  }
}
