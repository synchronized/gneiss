// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_GNEISS_H_
#define GNEISS_GNEISS_H_

#include <stdint.h>

#include <gneiss/core/entity.h>
#include <gneiss/core/export.h>
#include <gneiss/core/result.h>
#include <gneiss/core/rid.h>
#include <gneiss/core/version.h>
#include <gneiss/world.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 返回运行时 Gneiss 主版本号。 */
GNEISS_API uint32_t gneiss_version_major(void);

/** 返回运行时 Gneiss 次版本号。 */
GNEISS_API uint32_t gneiss_version_minor(void);

/** 返回运行时 Gneiss 修订版本号。 */
GNEISS_API uint32_t gneiss_version_patch(void);

#ifdef __cplusplus
}
#endif

#endif
