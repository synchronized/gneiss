// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/gneiss.hpp>

#include <cstdint>

int main() {
  static_assert(sizeof(gneiss::result) == sizeof(std::int32_t));
  static_assert(gneiss::succeeded(gneiss::result::success));
  static_assert(!gneiss::failed(gneiss::result::success));
  static_assert(gneiss::failed(gneiss::result::invalid_argument));
  static_assert(gneiss::to_native(gneiss::result::invalid_handle) == GNEISS_ERROR_INVALID_HANDLE);
  static_assert(gneiss::from_native(GNEISS_ERROR_NOT_READY) == gneiss::result::not_ready);

  return gneiss::result_message(gneiss::result::dependency_failed) == "dependency failed" ? 0 : 1;
}
