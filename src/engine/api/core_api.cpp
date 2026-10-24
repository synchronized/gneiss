// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/api/result_conversion.hpp"
#include "engine/core/result.hpp"
#include "engine/core/version.hpp"

#include <gneiss/gneiss.h>

extern "C" const char* gneiss_result_message(gneiss_result value) {
  return gneiss::engine::result_message(gneiss::engine::detail::from_c_result(value));
}
extern "C" uint32_t gneiss_version_major(void) { return gneiss::engine::library_version().major; }
extern "C" uint32_t gneiss_version_minor(void) { return gneiss::engine::library_version().minor; }
extern "C" uint32_t gneiss_version_patch(void) { return gneiss::engine::library_version().patch; }
