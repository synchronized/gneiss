// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/game/game_context_internal.hpp"

#include <gneiss/application.h>
#include <gneiss/game_module.hpp>
#include <gneiss/log.hpp>

#include <mutex>
#include <string>
#include <thread>

namespace {

std::uint64_t clock_value{};

struct log_capture final {
  std::mutex mutex;
  std::uint64_t count = 0U;
  std::string source;
  std::string message;
};

void capture_log(gneiss_application, const gneiss_log_event* event, void* user_data) {
  auto& capture = *static_cast<log_capture*>(user_data);
  const std::scoped_lock lock(capture.mutex);
  ++capture.count;
  capture.source.assign(event->source, event->source_length);
  capture.message.assign(event->message, event->message_length);
}

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
  log_capture capture;
  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  constexpr std::string_view asset_root = GNEISS_TEST_ASSET_ROOT;
  desc.asset_root = asset_root.data();
  desc.asset_root_length = static_cast<std::uint32_t>(asset_root.size());
  desc.user_data = &capture;
  desc.log = capture_log;
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
      gneiss::game_internal::set_game_context_log_source(context, "gneiss.test.module") !=
          GNEISS_SUCCESS ||
      context == GNEISS_NULL_GAME_CONTEXT) {
    return 2;
  }

  gneiss_world actual_world = GNEISS_NULL_WORLD;
  const gneiss::game_context borrowed{context};
  gneiss::world_ref typed_world;
  gneiss::entity_id typed_root;
  gneiss::action_id typed_action;
  gneiss::action_state typed_state = GNEISS_ACTION_STATE_INIT;
  if (borrowed.get_world(typed_world).failed() || typed_world.get() != expected_world ||
      borrowed.get_startup_root_entity(typed_root).failed() || typed_root.get() != expected_root ||
      gneiss::load_action_map(application, "asset://input/default.input-map.json").failed() ||
      borrowed.find_action("move_horizontal", typed_action).failed() || !typed_action.is_valid() ||
      borrowed.get_action_state(typed_action, typed_state).failed() || typed_state.held != 0U) {
    return 7;
  }
  const auto original_action = typed_action;
  if (borrowed.find_action("missing", typed_action) != gneiss::result::not_found ||
      typed_action != original_action) {
    return 8;
  }
  gneiss_entity_id actual_root = GNEISS_NULL_ENTITY_ID;
  gneiss_action action = GNEISS_NULL_ACTION;
  if (gneiss_game_context_get_world(context, &actual_world) != GNEISS_SUCCESS ||
      actual_world != expected_world ||
      gneiss_game_context_get_startup_root_entity(context, &actual_root) != GNEISS_SUCCESS ||
      actual_root != expected_root ||
      gneiss_game_context_find_action(context, "missing", UINT64_C(7), &action) !=
          GNEISS_ERROR_NOT_FOUND ||
      borrowed.request_exit().failed()) {
    return 3;
  }

  // 上下文身份先于动作参数校验；普通 World 查询则先检查输出指针。
  action = 7U;
  gneiss_action_state invalid_action_state = GNEISS_ACTION_STATE_INIT;
  invalid_action_state.struct_size = 0U;
  if (gneiss_game_context_find_action(context, nullptr, 0U, &action) !=
          GNEISS_ERROR_INVALID_ARGUMENT ||
      action != 7U ||
      gneiss_game_context_find_action(GNEISS_NULL_GAME_CONTEXT, nullptr, 0U, nullptr) !=
          GNEISS_ERROR_INVALID_HANDLE ||
      gneiss_game_context_get_action_state(context, 0U, &invalid_action_state) !=
          GNEISS_ERROR_INVALID_ARGUMENT ||
      gneiss_game_context_get_world(GNEISS_NULL_GAME_CONTEXT, nullptr) !=
          GNEISS_ERROR_INVALID_ARGUMENT) {
    return 6;
  }

  gneiss_result cross_thread_result = GNEISS_SUCCESS;
  gneiss::result typed_thread_result;
  gneiss_result cross_thread_log_result = GNEISS_ERROR_INTERNAL;
  const auto message = gneiss::make_log_message(gneiss::log_severity::info, "test", "worker ready");
  std::thread other([&] {
    cross_thread_result = gneiss_game_context_get_world(context, &actual_world);
    typed_thread_result = borrowed.get_world(typed_world);
    cross_thread_log_result = borrowed.log(message).native();
  });
  other.join();
  if (cross_thread_result != GNEISS_ERROR_INVALID_HANDLE ||
      typed_thread_result != gneiss::result::invalid_handle ||
      typed_world.get() != expected_world || cross_thread_log_result != GNEISS_SUCCESS ||
      gneiss::game_internal::destroy_game_context(context) != GNEISS_SUCCESS ||
      borrowed.get_world(typed_world) != gneiss::result::invalid_handle ||
      typed_world.get() != expected_world ||
      borrowed.get_startup_root_entity(typed_root) != gneiss::result::invalid_handle ||
      typed_root.get() != expected_root ||
      borrowed.find_action("move_horizontal", typed_action) != gneiss::result::invalid_handle ||
      typed_action != original_action ||
      gneiss_game_context_get_world(context, &actual_world) != GNEISS_ERROR_INVALID_HANDLE ||
      gneiss_game_context_log(context, &message) != GNEISS_ERROR_INVALID_HANDLE ||
      gneiss::game_internal::destroy_game_context(context) != GNEISS_ERROR_INVALID_HANDLE ||
      gneiss_application_destroy(application) != GNEISS_SUCCESS) {
    return 4;
  }
  {
    const std::scoped_lock lock(capture.mutex);
    if (capture.count != 1U || capture.source != "gneiss.test.module" ||
        capture.message != "worker ready") {
      return 5;
    }
  }
  return 0;
}
