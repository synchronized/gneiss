// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/log.h>

_Static_assert(GNEISS_LOG_EVENT_VERSION_1_SIZE == sizeof(gneiss_log_event),
               "日志事件 ABI 大小必须稳定");
