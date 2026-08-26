// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/core/result.h>

extern "C" const char* gneiss_result_message(gneiss_result result) {
  switch (result) {
  case GNEISS_SUCCESS:
    return "success";
  case GNEISS_ERROR_UNKNOWN:
    return "unknown error";
  case GNEISS_ERROR_INVALID_ARGUMENT:
    return "invalid argument";
  case GNEISS_ERROR_INVALID_HANDLE:
    return "invalid handle";
  case GNEISS_ERROR_OUT_OF_MEMORY:
    return "out of memory";
  case GNEISS_ERROR_UNSUPPORTED:
    return "unsupported operation";
  case GNEISS_ERROR_INITIALIZATION_FAILED:
    return "initialization failed";
  case GNEISS_ERROR_DEPENDENCY_FAILED:
    return "dependency failed";
  case GNEISS_ERROR_INVALID_STATE:
    return "invalid state";
  case GNEISS_ERROR_NOT_READY:
    return "operation temporarily not ready";
  case GNEISS_ERROR_INTERNAL:
    return "internal error";
  case GNEISS_ERROR_NOT_FOUND:
    return "not found";
  case GNEISS_ERROR_IO:
    return "I/O error";
  default:
    return "unrecognized result";
  }
}
