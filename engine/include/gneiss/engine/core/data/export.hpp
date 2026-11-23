// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

// Data 的链接归属独立于 Engine；静态使用要求由对应目标传播。
#if defined(GNEISS_DATA_STATIC_DEFINE)
#define GNEISS_DATA_API
#elif defined(_WIN32) || defined(__CYGWIN__)
#if defined(GNEISS_BUILDING_DATA)
#define GNEISS_DATA_API __declspec(dllexport)
#else
#define GNEISS_DATA_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define GNEISS_DATA_API __attribute__((visibility("default")))
#else
#define GNEISS_DATA_API
#endif
