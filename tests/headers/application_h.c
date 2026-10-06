// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/application.h>

_Static_assert(sizeof(gneiss_diagnostic) == 64, "诊断结构 ABI 大小必须稳定");
_Static_assert(GNEISS_APPLICATION_DESC_VERSION_6_SIZE == sizeof(gneiss_application_desc),
               "Application v6 描述大小必须稳定");
