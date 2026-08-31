// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "game/game_context_internal.h"

#include <gneiss/application.h>
#include <gneiss/game_module.h>

#include <thread>

namespace {

std::uint64_t clock_value{};

std::uint64_t now_ns(void*) {
  clock_value += 16'000'000;
  return clock_value;
}

gneiss_result poll_events(void*, std::uint8_t* out_should_close) {
  *out_should_close = 0;
  return GNEISS_SUCCESS;
}

gneiss_result update(gneiss_application, const gneiss_frame_time*, void*) { return GNEISS_SUCCESS; }

} // namespace

int main() {
  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  desc.now_ns = now_ns;
  desc.poll_events = poll_events;
  desc.update = update;
  gneiss_application application = GNEISS_NULL_APPLICATION;
  if (gneiss_application_create(&desc, &application) != GNEISS_SUCCESS) {
    return 1;
  }

  gneiss_world expected_world = GNEISS_NULL_WORLD;
  gneiss_entity_id expected_root = GNEISS_NULL_ENTITY_ID;
  gneiss_game_context context = GNEISS_NULL_GAME_CONTEXT;
  if (gneiss_application_get_world(application, &expected_world) != GNEISS_SUCCESS ||
      gneiss_world_entity_create(expected_world, &expected_root) != GNEISS_SUCCESS ||
      gneiss::game_internal::create_game_context(application, expected_root, &context) !=
          GNEISS_SUCCESS ||
      context == GNEISS_NULL_GAME_CONTEXT) {
    return 2;
  }

  gneiss_world actual_world = GNEISS_NULL_WORLD;
  gneiss_entity_id actual_root = GNEISS_NULL_ENTITY_ID;
  gneiss_action action = GNEISS_NULL_ACTION;
  if (gneiss_game_context_get_world(context, &actual_world) != GNEISS_SUCCESS ||
      actual_world != expected_world ||
      gneiss_game_context_get_startup_root_entity(context, &actual_root) != GNEISS_SUCCESS ||
      actual_root != expected_root ||
      gneiss_game_context_find_action(context, "missing", UINT64_C(7), &action) !=
          GNEISS_ERROR_NOT_FOUND ||
      gneiss_game_context_request_exit(context) != GNEISS_SUCCESS) {
    return 3;
  }

  gneiss_result cross_thread_result = GNEISS_SUCCESS;
  std::thread other(
      [&] { cross_thread_result = gneiss_game_context_get_world(context, &actual_world); });
  other.join();
  if (cross_thread_result != GNEISS_ERROR_INVALID_HANDLE ||
      gneiss::game_internal::destroy_game_context(context) != GNEISS_SUCCESS ||
      gneiss_game_context_get_world(context, &actual_world) != GNEISS_ERROR_INVALID_HANDLE ||
      gneiss::game_internal::destroy_game_context(context) != GNEISS_ERROR_INVALID_HANDLE ||
      gneiss_application_destroy(application) != GNEISS_SUCCESS) {
    return 4;
  }
  return 0;
}
