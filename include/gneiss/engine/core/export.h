// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_CORE_EXPORT_H_
#define GNEISS_CORE_EXPORT_H_

/** 标记仍可能演进的公共接口；不表示弃用，且默认不改变调用约定。 */
#ifndef GNEISS_EXPERIMENTAL
#define GNEISS_EXPERIMENTAL
#endif

#if defined(_WIN32) || defined(__CYGWIN__)
#if defined(GNEISS_STATIC_DEFINE)
#define GNEISS_API
#elif defined(GNEISS_BUILDING_LIBRARY)
#define GNEISS_API __declspec(dllexport)
#else
#define GNEISS_API __declspec(dllimport)
#endif
#define GNEISS_LOCAL
#else
#if defined(GNEISS_STATIC_DEFINE)
#define GNEISS_API
#elif defined(GNEISS_BUILDING_LIBRARY) && (defined(__GNUC__) || defined(__clang__))
#define GNEISS_API __attribute__((visibility("default")))
#else
#define GNEISS_API
#endif
#if defined(__GNUC__) || defined(__clang__)
#define GNEISS_LOCAL __attribute__((visibility("hidden")))
#else
#define GNEISS_LOCAL
#endif
#endif

#endif
