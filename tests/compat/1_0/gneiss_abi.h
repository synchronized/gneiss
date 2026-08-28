// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_TESTS_COMPAT_1_0_GNEISS_ABI_H_
#define GNEISS_TESTS_COMPAT_1_0_GNEISS_ABI_H_

#include <stddef.h>
#include <stdint.h>

#if defined(GNEISS_STATIC_DEFINE)
#define GNEISS_BASELINE_API
#elif defined(_WIN32)
#define GNEISS_BASELINE_API __declspec(dllimport)
#else
#define GNEISS_BASELINE_API __attribute__((visibility("default")))
#endif

typedef int32_t gneiss_result;
typedef uint64_t gneiss_application;

#define GNEISS_SUCCESS INT32_C(0)
#define GNEISS_ERROR_INVALID_ARGUMENT INT32_C(-2)
#define GNEISS_NULL_APPLICATION UINT64_C(0)
#define GNEISS_APPLICATION_PLATFORM_CALLBACK UINT32_C(0)
#define GNEISS_APPLICATION_WINDOW_VISIBLE_BIT (UINT32_C(1) << 0)
#define GNEISS_APPLICATION_WINDOW_RESIZABLE_BIT (UINT32_C(1) << 1)
#define GNEISS_APPLICATION_WINDOW_HIGH_DPI_BIT (UINT32_C(1) << 2)

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
typedef uint8_t (*gneiss_application_close_requested_fn)(gneiss_application application,
                                                         void* user_data);
typedef void (*gneiss_application_diagnostic_fn)(gneiss_application application,
                                                 const void* diagnostic, void* user_data);

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
  const char* asset_root;
  uint32_t asset_root_length;
  uint32_t asset_reserved;
  gneiss_application_diagnostic_fn diagnostic;
  gneiss_application_close_requested_fn close_requested;
} gneiss_application_desc;

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
   NULL}

#ifdef __cplusplus
extern "C" {
#endif

GNEISS_BASELINE_API uint32_t gneiss_version_major(void);
GNEISS_BASELINE_API const char* gneiss_result_message(gneiss_result result);
GNEISS_BASELINE_API gneiss_result gneiss_application_create(const gneiss_application_desc* desc,
                                                            gneiss_application* out_application);
GNEISS_BASELINE_API gneiss_result gneiss_application_destroy(gneiss_application application);
GNEISS_BASELINE_API gneiss_result gneiss_application_run(gneiss_application application,
                                                         uint64_t max_frame_count);
GNEISS_BASELINE_API gneiss_result gneiss_application_request_exit(gneiss_application application);

#ifdef __cplusplus
}
#endif

#endif
