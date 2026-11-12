// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

// 仅内部使用；目标需私有链接 gneiss_libassert。不要覆盖标准 assert 宏。
#include <libassert/assert.hpp>

// 不变量在所有配置检查；可恢复的输入错误仍返回 result/error。
#define GNEISS_ASSERT(...) LIBASSERT_ASSERT(__VA_ARGS__)
// Debug 检查，NDEBUG 时不求值；表达式与诊断参数均不得包含必要副作用。
#define GNEISS_DEBUG_ASSERT(...) LIBASSERT_DEBUG_ASSERT(__VA_ARGS__)
#define GNEISS_PANIC(...) LIBASSERT_PANIC(__VA_ARGS__)
