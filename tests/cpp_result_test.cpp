// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/gneiss.hpp>

#include <cstdint>
#include <type_traits>

int main() {
  static_assert(sizeof(gneiss::result) == sizeof(std::int32_t));
  static_assert(std::is_trivially_copyable_v<gneiss::result>);
  static_assert(std::is_standard_layout_v<gneiss::result>);

  static_assert(!gneiss::result{}.ok());
  static_assert(gneiss::result{}.failed());
  static_assert(gneiss::result{} == gneiss::result::unknown);
  static_assert(gneiss::result::success);
  static_assert(!gneiss::result::invalid_argument);
  static_assert(static_cast<bool>(gneiss::result::success));
  static_assert(!static_cast<bool>(gneiss::result::invalid_argument));
  static_assert(gneiss::result::success != gneiss::result::not_ready);
  static_assert(gneiss::result::invalid_handle.native() == GNEISS_ERROR_INVALID_HANDLE);
  static_assert(static_cast<gneiss_result>(gneiss::result::not_found) == GNEISS_ERROR_NOT_FOUND);

  static_assert(gneiss::to_native(gneiss::result::invalid_handle) == GNEISS_ERROR_INVALID_HANDLE);
  static_assert(gneiss::from_native(GNEISS_ERROR_NOT_READY) == gneiss::result::not_ready);
  static_assert(gneiss::from_native(1).ok());
  static_assert(gneiss::from_native(-999).failed());
  static_assert(gneiss::to_native(gneiss::from_native(-999)) == -999);

  return gneiss::result::dependency_failed.message() == "dependency failed" ? 0 : 1;
}
