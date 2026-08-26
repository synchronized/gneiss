// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPLICATION_H_
#define GNEISS_APPLICATION_H_

#include <stddef.h>
#include <stdint.h>

#include <gneiss/core/export.h>
#include <gneiss/core/result.h>
#include <gneiss/world.h>

/** Application 的不透明句柄。零值表示无效 Application。 */
typedef uint64_t gneiss_application;

#define GNEISS_NULL_APPLICATION UINT64_C(0)

/** 单帧时间信息。纳秒值使用单调时钟，不表示系统时间。 */
typedef struct gneiss_frame_time {
  uint64_t frame_index;
  uint64_t delta_ns;
  uint64_t elapsed_ns;
  uint8_t is_paused;
  uint8_t reserved[7];
} gneiss_frame_time;

typedef gneiss_result (*gneiss_application_initialize_fn)(void* user_data);
typedef gneiss_result (*gneiss_application_poll_events_fn)(void* user_data,
                                                           uint8_t* out_should_close);
typedef uint64_t (*gneiss_application_now_ns_fn)(void* user_data);
typedef gneiss_result (*gneiss_application_update_fn)(gneiss_application application,
                                                      const gneiss_frame_time* time,
                                                      void* user_data);
typedef void (*gneiss_application_shutdown_fn)(void* user_data);

/**
 * Application 创建参数。
 *
 * 回调均在创建线程同步调用。user_data 由调用方持有，必须至少存活至 Application 销毁完成。
 */
typedef struct gneiss_application_desc {
  uint32_t struct_size;
  uint32_t reserved;
  void* user_data;
  gneiss_application_initialize_fn initialize;
  gneiss_application_poll_events_fn poll_events;
  gneiss_application_now_ns_fn now_ns;
  gneiss_application_update_fn update;
  gneiss_application_shutdown_fn shutdown;
} gneiss_application_desc;

#define GNEISS_APPLICATION_DESC_INIT                                                               \
  {(uint32_t)sizeof(gneiss_application_desc), UINT32_C(0), NULL, NULL, NULL, NULL, NULL, NULL}

#ifdef __cplusplus
extern "C" {
#endif

/** 创建 Application 及其 World；初始化失败时自动回滚已完成步骤。 */
GNEISS_API gneiss_result gneiss_application_create(const gneiss_application_desc* desc,
                                                   gneiss_application* out_application);

/** 逆序销毁 World、平台回调状态和 Application。 */
GNEISS_API gneiss_result gneiss_application_destroy(gneiss_application application);

/**
 * 运行主循环。max_frame_count 为零时持续运行至收到退出请求。
 *
 * 每帧依次轮询事件、计算时间并调用 update；任一回调失败时立即返回该错误。
 */
GNEISS_API gneiss_result gneiss_application_run(gneiss_application application,
                                                uint64_t max_frame_count);

/** 请求主循环在当前回调返回后退出；可从 Application 回调中调用。 */
GNEISS_API gneiss_result gneiss_application_request_exit(gneiss_application application);

/** 设置暂停状态；暂停期间仍轮询事件并调用 update，但 delta_ns 为零。 */
GNEISS_API gneiss_result gneiss_application_set_paused(gneiss_application application,
                                                       uint8_t is_paused);

/** 获取由 Application 独占拥有的借用 World 句柄。 */
GNEISS_API gneiss_result gneiss_application_get_world(gneiss_application application,
                                                      gneiss_world* out_world);

#ifdef __cplusplus
}
#endif

#endif
