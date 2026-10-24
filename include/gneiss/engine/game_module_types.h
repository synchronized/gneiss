// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_GAME_MODULE_TYPES_H_
#define GNEISS_GAME_MODULE_TYPES_H_

#include <gneiss/engine/core/entity.h>
#include <gneiss/engine/core/result.h>
#include <stddef.h>
#include <stdint.h>

/** World 的不透明句柄。零值始终表示无效 World。 */
typedef uint64_t gneiss_world;

#define GNEISS_NULL_WORLD UINT64_C(0)

/**
 * Scene Tree 中的局部或世界变换。旋转使用归一化的 (x, y, z, w) 四元数，缩放各轴不得为零。
 */
typedef struct gneiss_transform {
  float translation[3];
  float rotation[4];
  float scale[3];
} gneiss_transform;

#define GNEISS_TRANSFORM_IDENTITY {{0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F, 1.0F}, {1.0F, 1.0F, 1.0F}}

typedef uint64_t gneiss_action;
#define GNEISS_NULL_ACTION UINT64_C(0)

typedef struct gneiss_action_state {
  uint32_t struct_size;
  uint8_t pressed;
  uint8_t held;
  uint8_t released;
  uint8_t reserved_byte;
  float value;
  uint32_t reserved[5];
} gneiss_action_state;

#define GNEISS_ACTION_STATE_VERSION_1_SIZE ((uint32_t)offsetof(gneiss_action_state, reserved))
#define GNEISS_ACTION_STATE_INIT                                                                   \
  {(uint32_t)sizeof(gneiss_action_state), 0, 0, 0, 0, 0.0F, {0, 0, 0, 0, 0}}

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

#endif
