// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPLICATION_H_
#define GNEISS_APPLICATION_H_

#include <stddef.h>
#include <stdint.h>

#include <gneiss/core/export.h>
#include <gneiss/core/result.h>
#include <gneiss/log.h>
#include <gneiss/world.h>

/** Application 的不透明句柄。零值表示无效 Application。 */
typedef uint64_t gneiss_application;

#define GNEISS_NULL_APPLICATION UINT64_C(0)

/** 在 Engine 日志消费线程接收不可变事件；不得保存事件或字符串指针，也不得重入日志提交。 */
typedef void (*gneiss_application_log_fn)(gneiss_application application,
                                          const gneiss_log_event* event, void* user_data);

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
/** 平台请求关闭时返回非零允许退出；返回零时继续运行，由宿主稍后主动请求退出。 */
typedef uint8_t (*gneiss_application_close_requested_fn)(gneiss_application application,
                                                         void* user_data);

typedef uint32_t gneiss_diagnostic_severity;
#define GNEISS_DIAGNOSTIC_INFO UINT32_C(1)
#define GNEISS_DIAGNOSTIC_WARNING UINT32_C(2)
#define GNEISS_DIAGNOSTIC_ERROR UINT32_C(3)

typedef uint32_t gneiss_diagnostic_category;
#define GNEISS_DIAGNOSTIC_CATEGORY_APPLICATION UINT32_C(1)
#define GNEISS_DIAGNOSTIC_CATEGORY_ASSET UINT32_C(2)
#define GNEISS_DIAGNOSTIC_CATEGORY_INPUT UINT32_C(3)
#define GNEISS_DIAGNOSTIC_CATEGORY_BACKEND UINT32_C(4)

typedef struct gneiss_diagnostic {
  uint32_t struct_size;
  uint32_t severity;
  uint32_t category;
  gneiss_result result;
  const char* module;
  uint64_t module_length;
  const char* message;
  uint64_t message_length;
  uint64_t reserved[2];
} gneiss_diagnostic;

#define GNEISS_DIAGNOSTIC_VERSION_1_SIZE ((uint32_t)offsetof(gneiss_diagnostic, reserved))

typedef void (*gneiss_application_diagnostic_fn)(gneiss_application application,
                                                 const gneiss_diagnostic* diagnostic,
                                                 void* user_data);

/** Application 平台类型使用定宽整数，避免 C enum 的实现相关 ABI。 */
typedef uint32_t gneiss_application_platform;
/** 使用生命周期回调；全部为空时为无窗口模式。 */
#define GNEISS_APPLICATION_PLATFORM_CALLBACK UINT32_C(0)
/** 使用 Granit Window 组件创建并管理窗口。 */
#define GNEISS_APPLICATION_PLATFORM_GRANIT UINT32_C(1)

#define GNEISS_APPLICATION_WINDOW_VISIBLE_BIT (UINT32_C(1) << 0)
#define GNEISS_APPLICATION_WINDOW_RESIZABLE_BIT (UINT32_C(1) << 1)
#define GNEISS_APPLICATION_WINDOW_HIGH_DPI_BIT (UINT32_C(1) << 2)

/**
 * Application 创建参数。
 *
 * 除日志回调外，其余回调均在创建线程同步调用。日志回调在专用消费线程串行调用。user_data 由调用方
 * 持有，必须至少存活至 Application 销毁完成。
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
  uint32_t platform;
  const char* window_title;
  uint32_t window_title_length;
  uint32_t window_width;
  uint32_t window_height;
  uint32_t window_flags;
  /** 可选的 UTF-8 资产根目录。为空时不挂载目录资产。 */
  const char* asset_root;
  uint32_t asset_root_length;
  uint32_t asset_reserved;
  gneiss_application_diagnostic_fn diagnostic;
  gneiss_application_close_requested_fn close_requested;
  gneiss_application_log_fn log;
} gneiss_application_desc;

#define GNEISS_APPLICATION_DESC_VERSION_1_SIZE                                                     \
  ((uint32_t)(offsetof(gneiss_application_desc, shutdown) + sizeof(gneiss_application_shutdown_fn)))
#define GNEISS_APPLICATION_DESC_VERSION_2_SIZE                                                     \
  ((uint32_t)(offsetof(gneiss_application_desc, asset_reserved) + sizeof(uint32_t)))
#define GNEISS_APPLICATION_DESC_VERSION_3_SIZE                                                     \
  ((uint32_t)(offsetof(gneiss_application_desc, diagnostic) +                                      \
              sizeof(gneiss_application_diagnostic_fn)))
#define GNEISS_APPLICATION_DESC_VERSION_4_SIZE                                                     \
  ((uint32_t)(offsetof(gneiss_application_desc, close_requested) +                                 \
              sizeof(gneiss_application_close_requested_fn)))
#define GNEISS_APPLICATION_DESC_VERSION_5_SIZE                                                     \
  ((uint32_t)(offsetof(gneiss_application_desc, log) + sizeof(gneiss_application_log_fn)))

#define GNEISS_APPLICATION_DESC_INIT                                                               \
  {(uint32_t)sizeof(gneiss_application_desc),                                                      \
   UINT32_C(0),                                                                                    \
   NULL,                                                                                           \
   NULL,                                                                                           \
   NULL,                                                                                           \
   NULL,                                                                                           \
   NULL,                                                                                           \
   NULL,                                                                                           \
   GNEISS_APPLICATION_PLATFORM_CALLBACK,                                                           \
   NULL,                                                                                           \
   UINT32_C(0),                                                                                    \
   UINT32_C(1280),                                                                                 \
   UINT32_C(720),                                                                                  \
   GNEISS_APPLICATION_WINDOW_VISIBLE_BIT | GNEISS_APPLICATION_WINDOW_RESIZABLE_BIT |               \
       GNEISS_APPLICATION_WINDOW_HIGH_DPI_BIT,                                                     \
   NULL,                                                                                           \
   UINT32_C(0),                                                                                    \
   UINT32_C(0),                                                                                    \
   NULL,                                                                                           \
   NULL,                                                                                           \
   NULL}

#ifdef __cplusplus
extern "C" {
#endif

/** 创建 Application 及其 World；初始化失败时自动回滚已完成步骤。 */
GNEISS_API gneiss_result gneiss_application_create(const gneiss_application_desc* desc,
                                                   gneiss_application* out_application);

/**
 * 逆序销毁 World、平台回调状态、Render Service、平台窗口和 Application。
 *
 * Granit 模式下会在销毁 Renderer 前检查 GPU 逻辑资源；仍有存活资源时仍完成清理，但返回
 * GNEISS_ERROR_INVALID_STATE，并通过诊断回调报告分类计数。仅限创建线程调用。
 */
GNEISS_API gneiss_result gneiss_application_destroy(gneiss_application application);

/**
 * 运行主循环。max_frame_count 为零时持续运行至收到退出请求。
 *
 * 每帧依次轮询事件、计算时间、调用 update，并在 Granit 模式下呈现；失败时立即返回错误。
 */
GNEISS_API gneiss_result gneiss_application_run(gneiss_application application,
                                                uint64_t max_frame_count);

/** 请求主循环在当前回调返回后退出；可从 Application 回调中调用。 */
GNEISS_API gneiss_result gneiss_application_request_exit(gneiss_application application);

/** 设置暂停状态；暂停期间仍轮询事件并调用 update，但 delta_ns 为零。 */
GNEISS_API gneiss_result gneiss_application_set_paused(gneiss_application application,
                                                       uint8_t is_paused);

/**
 * 提交日志消息并在返回前完成字符串复制。
 *
 * 可从任意线程调用；接收回调由专用消费线程串行执行。队列已满时丢弃新事件并通过后续告警报告数量。
 * 未设置接收回调时仍校验消息并返回成功；接收回调重入本函数返回 GNEISS_ERROR_INVALID_STATE。
 */
GNEISS_EXPERIMENTAL GNEISS_API gneiss_result
gneiss_application_log(gneiss_application application, const gneiss_log_message* message);

/** 获取由 Application 独占拥有的借用 World 句柄。 */
GNEISS_API gneiss_result gneiss_application_get_world(gneiss_application application,
                                                      gneiss_world* out_world);

#ifdef __cplusplus
}
#endif

#endif
