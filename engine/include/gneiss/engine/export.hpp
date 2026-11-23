// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_ENGINE_EXPORT_HPP_
#define GNEISS_ENGINE_EXPORT_HPP_

// 仅控制 gneiss_engine 的符号可见性；各基础模块使用自己的导出宏，不构成公共 SDK。
#if defined(_WIN32) || defined(__CYGWIN__)
#if defined(GNEISS_ENGINE_STATIC_DEFINE)
#define GNEISS_ENGINE_API
#elif defined(GNEISS_BUILDING_ENGINE)
#define GNEISS_ENGINE_API __declspec(dllexport)
#else
#define GNEISS_ENGINE_API __declspec(dllimport)
#endif
#define GNEISS_ENGINE_LOCAL
#else
#if defined(GNEISS_ENGINE_STATIC_DEFINE)
#define GNEISS_ENGINE_API
#elif defined(GNEISS_BUILDING_ENGINE) && (defined(__GNUC__) || defined(__clang__))
#define GNEISS_ENGINE_API __attribute__((visibility("default")))
#else
#define GNEISS_ENGINE_API
#endif
#if defined(__GNUC__) || defined(__clang__)
#define GNEISS_ENGINE_LOCAL __attribute__((visibility("hidden")))
#else
#define GNEISS_ENGINE_LOCAL
#endif
#endif

#endif
