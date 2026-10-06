// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_CORE_ENTITY_H_
#define GNEISS_CORE_ENTITY_H_

#include <stdint.h>

/** World 内实体的不透明运行时标识。零值始终表示无效实体。 */
typedef uint64_t gneiss_entity_id;

#define GNEISS_NULL_ENTITY_ID UINT64_C(0)

#endif
