// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_TESTS_COMPAT_0_9_GNEISS_ABI_H_
#define GNEISS_TESTS_COMPAT_0_9_GNEISS_ABI_H_

#include <stddef.h>
#include <stdint.h>

#if defined(GNEISS_STATIC_DEFINE)
#define GNEISS_0_9_API
#elif defined(_WIN32)
#define GNEISS_0_9_API __declspec(dllimport)
#else
#define GNEISS_0_9_API __attribute__((visibility("default")))
#endif

typedef int32_t gneiss_result;
typedef uint64_t gneiss_application;

#define GNEISS_SUCCESS INT32_C(0)
#define GNEISS_ERROR_INVALID_ARGUMENT INT32_C(-2)
#define GNEISS_NULL_APPLICATION UINT64_C(0)

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

/** v0.9.0 的 Application v1 描述布局，只包含首个已发布结构版本。 */
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

GNEISS_0_9_API uint32_t gneiss_version_major(void);
GNEISS_0_9_API const char* gneiss_result_message(gneiss_result result);
GNEISS_0_9_API gneiss_result gneiss_application_create(const gneiss_application_desc* desc,
                                                       gneiss_application* out_application);
GNEISS_0_9_API gneiss_result gneiss_application_destroy(gneiss_application application);
GNEISS_0_9_API gneiss_result gneiss_application_run(gneiss_application application,
                                                    uint64_t max_frame_count);
GNEISS_0_9_API gneiss_result gneiss_application_request_exit(gneiss_application application);

#ifdef __cplusplus
}
#endif

#endif
