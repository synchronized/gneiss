// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

// Meta 的链接归属独立于 Engine；静态使用要求由对应目标传播。
#if defined(GNEISS_META_STATIC_DEFINE)
#define GNEISS_META_API
#elif defined(_WIN32) || defined(__CYGWIN__)
#if defined(GNEISS_BUILDING_META)
#define GNEISS_META_API __declspec(dllexport)
#else
#define GNEISS_META_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define GNEISS_META_API __attribute__((visibility("default")))
#else
#define GNEISS_META_API
#endif
