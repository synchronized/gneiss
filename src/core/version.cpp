// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/gneiss.h>

extern "C" uint32_t gneiss_version_major(void) { return GNEISS_VERSION_MAJOR; }

extern "C" uint32_t gneiss_version_minor(void) { return GNEISS_VERSION_MINOR; }

extern "C" uint32_t gneiss_version_patch(void) { return GNEISS_VERSION_PATCH; }
