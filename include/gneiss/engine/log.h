// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_LOG_H_
#define GNEISS_LOG_H_

#include <stddef.h>
#include <stdint.h>

#include <gneiss/engine/core/export.h>
#include <gneiss/engine/core/result.h>
#include <gneiss/engine/game_module_types.h>

/**
 * Engine 补全后交给日志接收方的不可变事件。
 *
 * 所有字符串及事件本身只在接收回调期间有效。时间戳来自单调时钟，不表示系统时间；thread_id 是仅供
 * 同一进程内关联的非零标识，不对应操作系统线程号。
 */
typedef struct gneiss_log_event {
  uint32_t struct_size;
  uint32_t severity;
  uint64_t sequence;
  uint64_t timestamp_ns;
  uint64_t thread_id;
  const char* source;
  uint64_t source_length;
  const char* category;
  uint64_t category_length;
  const char* message;
  uint64_t message_length;
  gneiss_result result;
  uint32_t flags;
  uint64_t reserved[2];
} gneiss_log_event;

#define GNEISS_LOG_EVENT_VERSION_1_SIZE                                                            \
  ((uint32_t)(offsetof(gneiss_log_event, reserved) + sizeof(uint64_t) * 2U))

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
