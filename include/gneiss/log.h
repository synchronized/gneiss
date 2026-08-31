// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_LOG_H_
#define GNEISS_LOG_H_

#include <stddef.h>
#include <stdint.h>

#include <gneiss/core/export.h>
#include <gneiss/core/result.h>

/** 日志严重级别使用定宽整数，数值越大表示越严重。 */
typedef uint32_t gneiss_log_severity;
#define GNEISS_LOG_TRACE UINT32_C(1)
#define GNEISS_LOG_DEBUG UINT32_C(2)
#define GNEISS_LOG_INFO UINT32_C(3)
#define GNEISS_LOG_WARNING UINT32_C(4)
#define GNEISS_LOG_ERROR UINT32_C(5)
#define GNEISS_LOG_FATAL UINT32_C(6)

/**
 * 日志生产者提交的 UTF-8 消息。
 *
 * category 用于稳定筛选，不应包含用户数据；message 可以为空。调用期间只借用字符串，提交接口
 * 返回后调用方可以立即释放。时间、线程和可信来源由 Engine 接收消息时补充。
 */
typedef struct gneiss_log_message {
  uint32_t struct_size;
  uint32_t severity;
  const char* category;
  uint64_t category_length;
  const char* message;
  uint64_t message_length;
  gneiss_result result;
  uint32_t flags;
  uint64_t reserved[2];
} gneiss_log_message;

#define GNEISS_LOG_MESSAGE_VERSION_1_SIZE                                                          \
  ((uint32_t)(offsetof(gneiss_log_message, reserved) + sizeof(uint64_t) * 2U))

#define GNEISS_LOG_MESSAGE_INIT                                                                    \
  {                                                                                                \
    (uint32_t)sizeof(gneiss_log_message), GNEISS_LOG_INFO, NULL, UINT64_C(0), NULL, UINT64_C(0),   \
        GNEISS_SUCCESS, UINT32_C(0), {                                                             \
      UINT64_C(0), UINT64_C(0)                                                                     \
    }                                                                                              \
  }

#ifdef __cplusplus
extern "C" {
#endif

/** 校验日志消息布局、级别、UTF-8 字段边界和保留字段；不取得消息所有权。线程安全。 */
GNEISS_EXPERIMENTAL GNEISS_API gneiss_result
gneiss_log_message_validate(const gneiss_log_message* message);

#ifdef __cplusplus
}
#endif

#endif
