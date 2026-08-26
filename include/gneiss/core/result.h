// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_CORE_RESULT_H_
#define GNEISS_CORE_RESULT_H_

#include <stdint.h>

#include <gneiss/core/export.h>

/** Gneiss 操作结果的定宽 ABI 类型。零表示成功，负值表示失败。 */
typedef int32_t gneiss_result;

#define GNEISS_SUCCESS INT32_C(0)
#define GNEISS_ERROR_UNKNOWN INT32_C(-1)
#define GNEISS_ERROR_INVALID_ARGUMENT INT32_C(-2)
#define GNEISS_ERROR_INVALID_HANDLE INT32_C(-3)
#define GNEISS_ERROR_OUT_OF_MEMORY INT32_C(-4)
#define GNEISS_ERROR_UNSUPPORTED INT32_C(-5)
#define GNEISS_ERROR_INITIALIZATION_FAILED INT32_C(-6)
#define GNEISS_ERROR_DEPENDENCY_FAILED INT32_C(-7)
#define GNEISS_ERROR_INVALID_STATE INT32_C(-8)
#define GNEISS_ERROR_NOT_READY INT32_C(-9)
#define GNEISS_ERROR_INTERNAL INT32_C(-10)

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 返回结果码对应的静态英文文本。
 *
 * 返回的字符串由 Gneiss 持有，调用者不得释放。该函数线程安全且不返回空指针。
 */
GNEISS_API const char* gneiss_result_message(gneiss_result result);

#ifdef __cplusplus
}
#endif

#endif
