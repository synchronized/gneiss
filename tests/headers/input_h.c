// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/input.h>

_Static_assert(sizeof(gneiss_input_event_data) == 64, "输入事件负载 ABI 大小必须稳定");
_Static_assert(sizeof(gneiss_input_event) == 88, "输入事件 ABI 大小必须稳定");
_Static_assert(sizeof(gneiss_keyboard_state) == 64, "键盘状态 ABI 大小必须稳定");
_Static_assert(sizeof(gneiss_pointer_state) == 40, "指针状态 ABI 大小必须稳定");
_Static_assert(sizeof(gneiss_action_state) == 32, "动作状态 ABI 大小必须稳定");
