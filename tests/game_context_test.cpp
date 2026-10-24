// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "runtime/game/game_context_internal.hpp"

#include <gneiss/engine/application.h>
#include <gneiss/engine/game_module.h>
#include <gneiss/engine/input.hpp>
#include <gneiss/engine/log.hpp>

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
  if (gneiss::engine::core::result{gneiss_application_create(&desc, &application)} !=
      gneiss::engine::core::result::success) {
    return 1;
  }

  gneiss_world expected_world = GNEISS_NULL_WORLD;
  gneiss_entity_id expected_root = GNEISS_NULL_ENTITY_ID;
  gneiss_game_context context = GNEISS_NULL_GAME_CONTEXT;
  if (gneiss::engine::core::result{gneiss_application_get_world(application, &expected_world)} !=
          gneiss::engine::core::result::success ||
      gneiss::engine::core::result{gneiss_world_entity_create(expected_world, &expected_root)} !=
          gneiss::engine::core::result::success ||
      gneiss::runtime::game::create_game_context(application, expected_root, &context) !=
          gneiss::engine::core::result::success ||
      gneiss::runtime::game::set_game_context_log_source(context, "gneiss.test.module") !=
          gneiss::engine::core::result::success ||
      context == GNEISS_NULL_GAME_CONTEXT) {
    return 2;
  }

  gneiss_world actual_world = GNEISS_NULL_WORLD;
  if (gneiss::load_action_map(application, "asset://input/default.input-map.json").failed()) {
    return 7;
  }
  gneiss_entity_id actual_root = GNEISS_NULL_ENTITY_ID;
  gneiss_action action = GNEISS_NULL_ACTION;
  if (gneiss::engine::core::result{gneiss_game_context_get_world(context, &actual_world)} !=
          gneiss::engine::core::result::success ||
      actual_world != expected_world ||
      gneiss::engine::core::result{gneiss_game_context_get_startup_root_entity(
          context, &actual_root)} != gneiss::engine::core::result::success ||
      actual_root != expected_root ||
      gneiss::engine::core::result{gneiss_game_context_find_action(
          context, "missing", UINT64_C(7), &action)} != gneiss::engine::core::result::not_found ||
      gneiss::engine::core::result{gneiss_game_context_request_exit(context)} !=
          gneiss::engine::core::result::success) {
    return 3;
  }

  gneiss_action_state action_state = GNEISS_ACTION_STATE_INIT;
  if (gneiss::engine::core::result{gneiss_game_context_find_action(
          context, "move_horizontal", 15U, &action)} != gneiss::engine::core::result::success ||
      action == GNEISS_NULL_ACTION ||
      gneiss::engine::core::result{gneiss_game_context_get_action_state(
          context, action, &action_state)} != gneiss::engine::core::result::success ||
      action_state.held != 0U) {
    return 8;
  }

  // 上下文身份先于动作参数校验；普通 World 查询则先检查输出指针。
  action = 7U;
  gneiss_action_state invalid_action_state = GNEISS_ACTION_STATE_INIT;
  invalid_action_state.struct_size = 0U;
  if (gneiss::engine::core::result{gneiss_game_context_find_action(
          context, nullptr, 0U, &action)} != gneiss::engine::core::result::invalid_argument ||
      action != 7U ||
      gneiss::engine::core::result{
          gneiss_game_context_find_action(GNEISS_NULL_GAME_CONTEXT, nullptr, 0U, nullptr)} !=
          gneiss::engine::core::result::invalid_handle ||
      gneiss::engine::core::result{gneiss_game_context_get_action_state(
          context, 0U, &invalid_action_state)} != gneiss::engine::core::result::invalid_argument ||
      gneiss::engine::core::result{gneiss_game_context_get_world(
          GNEISS_NULL_GAME_CONTEXT, nullptr)} != gneiss::engine::core::result::invalid_argument) {
    return 6;
  }

  gneiss::engine::core::result cross_thread_result = gneiss::engine::core::result::success;
  gneiss::engine::core::result cross_thread_log_result = gneiss::engine::core::result::internal;
  const auto message = gneiss::make_log_message(gneiss::log_severity::info, "test", "worker ready");
  const auto native_message = gneiss::to_native(message);
  std::thread other([&] {
    cross_thread_result =
        gneiss::engine::core::result{gneiss_game_context_get_world(context, &actual_world)};
    cross_thread_log_result =
        gneiss::engine::core::result{gneiss_game_context_log(context, &native_message)};
  });
  other.join();
  if (cross_thread_result != gneiss::engine::core::result::invalid_handle ||
      cross_thread_log_result != gneiss::engine::core::result::success ||
      gneiss::runtime::game::destroy_game_context(context) !=
          gneiss::engine::core::result::success ||
      gneiss::engine::core::result{gneiss_game_context_get_world(context, &actual_world)} !=
          gneiss::engine::core::result::invalid_handle ||
      gneiss::engine::core::result{gneiss_game_context_log(context, &native_message)} !=
          gneiss::engine::core::result::invalid_handle ||
      gneiss::runtime::game::destroy_game_context(context) !=
          gneiss::engine::core::result::invalid_handle ||
      gneiss::engine::core::result{gneiss_application_destroy(application)} !=
          gneiss::engine::core::result::success) {
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
