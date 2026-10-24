// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_WORLD_H_
#define GNEISS_WORLD_H_

#include <stdint.h>

#include <gneiss/engine/core/entity.h>
#include <gneiss/engine/core/export.h>
#include <gneiss/engine/core/result.h>
#include <gneiss/engine/game_module_types.h>

#define GNEISS_TRANSFORM_FIELD_TRANSLATION UINT32_C(1)
#define GNEISS_TRANSFORM_FIELD_ROTATION UINT32_C(2)
#define GNEISS_TRANSFORM_FIELD_SCALE UINT32_C(3)
#define GNEISS_CAMERA_FIELD_VERTICAL_FIELD_OF_VIEW_RADIANS UINT32_C(1)
#define GNEISS_CAMERA_FIELD_NEAR_PLANE UINT32_C(2)
#define GNEISS_CAMERA_FIELD_FAR_PLANE UINT32_C(3)
#define GNEISS_CAMERA_FIELD_IS_PRIMARY UINT32_C(4)

#endif
