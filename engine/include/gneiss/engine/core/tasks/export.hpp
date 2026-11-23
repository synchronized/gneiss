// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

// Math 的链接归属独立于 Engine；静态使用要求由对应目标传播。
#if defined(GNEISS_TASKS_STATIC_DEFINE)
#define GNEISS_TASKS_API
#elif defined(_WIN32) || defined(__CYGWIN__)
#if defined(GNEISS_BUILDING_TASKS)
#define GNEISS_TASKS_API __declspec(dllexport)
#else
#define GNEISS_TASKS_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define GNEISS_TASKS_API __attribute__((visibility("default")))
#else
#define GNEISS_TASKS_API
#endif

// ELF/Mach-O 跨库多态类型需要可见 RTTI；Windows 保留逐成员导出，避免导出 STL 私有成员。
#if !defined(_WIN32) && !defined(__CYGWIN__)
#define GNEISS_TASKS_TYPE_API GNEISS_TASKS_API
#else
#define GNEISS_TASKS_TYPE_API
#endif
