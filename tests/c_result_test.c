// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/gneiss.h>

#include <string.h>

_Static_assert(sizeof(gneiss_result) == sizeof(int32_t), "gneiss_result 必须保持 32 位");

int main(void) {
  if (strcmp(gneiss_result_message(GNEISS_SUCCESS), "success") != 0) {
    return 1;
  }
  if (strcmp(gneiss_result_message(GNEISS_ERROR_INVALID_ARGUMENT), "invalid argument") != 0) {
    return 2;
  }
  if (strcmp(gneiss_result_message(INT32_C(-999)), "unrecognized result") != 0) {
    return 3;
  }
  return 0;
}
