// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_CORE_RID_H_
#define GNEISS_CORE_RID_H_

#include <stdint.h>

/** Gneiss Service 资源的不透明标识。零值始终表示无效 RID。 */
typedef uint64_t gneiss_rid;

#define GNEISS_NULL_RID UINT64_C(0)

#endif
