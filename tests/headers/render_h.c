// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/render.h>

_Static_assert(sizeof(gneiss_texture) == 8, "Texture RID 必须保持 64 位");
_Static_assert(sizeof(gneiss_camera) == 16, "旧 Camera ABI 必须保持不变");
_Static_assert(sizeof(gneiss_camera_desc) == 20, "Camera 描述布局必须稳定");
