// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/application.h>
#include <gneiss/input.h>

typedef struct test_context {
  uint64_t now_ns;
  uint64_t update_count;
  uint64_t shutdown_count;
  uint8_t fail_initialize;
  uint8_t should_close;
} test_context;

static gneiss_result initialize(void* user_data) {
  test_context* context = (test_context*)user_data;
  return context->fail_initialize != 0U ? GNEISS_ERROR_INITIALIZATION_FAILED : GNEISS_SUCCESS;
}

static uint64_t now_ns(void* user_data) {
  test_context* context = (test_context*)user_data;
  context->now_ns += UINT64_C(10);
  return context->now_ns;
}

static gneiss_result poll_events(void* user_data, uint8_t* out_should_close) {
  test_context* context = (test_context*)user_data;
  *out_should_close = context->should_close;
  return GNEISS_SUCCESS;
}

static gneiss_result update(gneiss_application application, const gneiss_frame_time* time,
                            void* user_data) {
  test_context* context = (test_context*)user_data;
  if (time->frame_index != context->update_count ||
      (time->frame_index > 0U && time->delta_ns != UINT64_C(10))) {
    return GNEISS_ERROR_INTERNAL;
  }
  ++context->update_count;
  if (context->update_count == UINT64_C(3)) {
    return gneiss_application_request_exit(application);
  }
  return GNEISS_SUCCESS;
}

static void shutdown(void* user_data) {
  test_context* context = (test_context*)user_data;
  ++context->shutdown_count;
}

int main(void) {
  test_context context = {0};
  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  gneiss_application application = GNEISS_NULL_APPLICATION;
  gneiss_world world = GNEISS_NULL_WORLD;
  desc.user_data = &context;
  desc.initialize = initialize;
  desc.poll_events = poll_events;
  desc.now_ns = now_ns;
  desc.update = update;
  desc.shutdown = shutdown;

  if (gneiss_application_create(&desc, &application) != GNEISS_SUCCESS ||
      gneiss_application_get_world(application, &world) != GNEISS_SUCCESS ||
      world == GNEISS_NULL_WORLD) {
    return 1;
  }

  {
    gneiss_input_event event = GNEISS_INPUT_EVENT_INIT;
    gneiss_keyboard_state keyboard = GNEISS_KEYBOARD_STATE_INIT;
    gneiss_pointer_state pointer = GNEISS_POINTER_STATE_INIT;
    if (gneiss_application_poll_input(application, &event) != GNEISS_ERROR_NOT_READY ||
        event.type != 0U ||
        gneiss_application_get_keyboard_state(application, &keyboard) != GNEISS_SUCCESS ||
        keyboard.modifiers != 0U || keyboard.pressed_keys[0] != 0U ||
        gneiss_application_get_pointer_state(application, &pointer) != GNEISS_SUCCESS ||
        pointer.buttons != 0U || pointer.is_inside != 0U) {
      return 1;
    }
  }

  if (gneiss_application_run(application, 0) != GNEISS_SUCCESS ||
      context.update_count != UINT64_C(3) ||
      gneiss_application_destroy(application) != GNEISS_SUCCESS || context.shutdown_count != 1U ||
      gneiss_world_entity_count(world, &context.update_count) != GNEISS_ERROR_INVALID_HANDLE) {
    return 1;
  }

  context.update_count = 0U;
  context.should_close = 1U;
  application = GNEISS_NULL_APPLICATION;
  if (gneiss_application_create(&desc, &application) != GNEISS_SUCCESS ||
      gneiss_application_run(application, 0) != GNEISS_SUCCESS || context.update_count != 0U ||
      gneiss_application_destroy(application) != GNEISS_SUCCESS || context.shutdown_count != 2U) {
    return 2;
  }

  desc.struct_size = GNEISS_APPLICATION_DESC_VERSION_1_SIZE;
  application = GNEISS_NULL_APPLICATION;
  if (gneiss_application_create(&desc, &application) != GNEISS_SUCCESS ||
      gneiss_application_destroy(application) != GNEISS_SUCCESS || context.shutdown_count != 3U) {
    return 3;
  }
  desc.struct_size = (uint32_t)sizeof(gneiss_application_desc);

  context.fail_initialize = 1U;
  context.should_close = 0U;
  application = GNEISS_NULL_APPLICATION;
  if (gneiss_application_create(&desc, &application) != GNEISS_ERROR_INITIALIZATION_FAILED ||
      application != GNEISS_NULL_APPLICATION || context.shutdown_count != 4U) {
    return 4;
  }
#ifdef GNEISS_TEST_NO_GRANIT_PLATFORM
  desc = (gneiss_application_desc)GNEISS_APPLICATION_DESC_INIT;
  desc.platform = GNEISS_APPLICATION_PLATFORM_GRANIT;
  if (gneiss_application_create(&desc, &application) != GNEISS_ERROR_UNSUPPORTED ||
      application != GNEISS_NULL_APPLICATION) {
    return 5;
  }
#endif
  return 0;
}
