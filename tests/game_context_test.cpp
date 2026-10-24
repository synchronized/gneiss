// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "engine/function/world/world.hpp"
#include "runtime/application.hpp"
#include "runtime/application_bridge.hpp"
#include "runtime/game/game_context_internal.hpp"
#include <gneiss/engine/game_module.h>

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

} // namespace

int main() {
  log_capture capture;
  gneiss::runtime::application_configuration config;
  config.asset_root = GNEISS_TEST_ASSET_ROOT;
  config.callbacks.log = [&](const gneiss::engine::event_view& event) {
    const std::scoped_lock lock(capture.mutex);
    ++capture.count;
    capture.source = event.message.source;
    capture.message = event.message.message;
  };
  config.callbacks.now_ns = [] {
    clock_value += 16'000'000;
    return clock_value;
  };
  config.callbacks.poll_events = [](bool& close) {
    close = false;
    return gneiss::engine::result::success;
  };
  config.callbacks.update = [](auto, const auto&) { return gneiss::engine::result::success; };
  std::shared_ptr<gneiss::runtime::application> native_application;
  gneiss::runtime::application_bridge bridge;
  if (gneiss::runtime::application::create(config, native_application).failed() ||
      bridge.bind(native_application).failed())
    return 1;
  const auto application = bridge.get();
  const auto expected_world = native_application->world();
  gneiss::entity_id root;
  gneiss::scene_node_id node;
  if (native_application->native_world()->entity_create(root).failed() ||
      native_application->native_world()->node_create({}, root, node).failed())
    return 2;
  const auto expected_root = root.get();
  gneiss_game_context context = GNEISS_NULL_GAME_CONTEXT;
  if (gneiss::runtime::create_game_context(application, expected_root, &context) !=
          gneiss::engine::result::success ||
      gneiss::runtime::set_game_context_log_source(context, "gneiss.test.module") !=
          gneiss::engine::result::success ||
      context == GNEISS_NULL_GAME_CONTEXT) {
    return 2;
  }

  gneiss_world actual_world = GNEISS_NULL_WORLD;
  if (native_application->load_action_map("asset://input/default.input-map.json").failed()) {
    return 7;
  }
  gneiss_entity_id actual_root = GNEISS_NULL_ENTITY_ID;
  gneiss_action action = GNEISS_NULL_ACTION;
  if (gneiss::engine::result{gneiss_game_context_get_world(context, &actual_world)} !=
          gneiss::engine::result::success ||
      actual_world != expected_world ||
      gneiss::engine::result{gneiss_game_context_get_startup_root_entity(context, &actual_root)} !=
          gneiss::engine::result::success ||
      actual_root != expected_root ||
      gneiss::engine::result{gneiss_game_context_find_action(
          context, "missing", UINT64_C(7), &action)} != gneiss::engine::result::not_found ||
      gneiss::engine::result{gneiss_game_context_request_exit(context)} !=
          gneiss::engine::result::success) {
    return 3;
  }

  gneiss_transform local = GNEISS_TRANSFORM_IDENTITY;
  local.translation[0] = 3.0F;
  gneiss_transform observed = GNEISS_TRANSFORM_IDENTITY;
  if (gneiss_game_context_set_entity_transform(context, expected_root, &local) != GNEISS_SUCCESS ||
      gneiss_game_context_get_entity_transform(context, expected_root, &observed) !=
          GNEISS_SUCCESS ||
      observed.translation[0] != 3.0F ||
      gneiss_game_context_get_entity_transform(context, expected_root, nullptr) !=
          GNEISS_ERROR_INVALID_ARGUMENT ||
      gneiss_game_context_get_entity_transform(context, 0, &observed) !=
          GNEISS_ERROR_INVALID_HANDLE ||
      gneiss_game_context_get_entity_transform(0, expected_root, &observed) !=
          GNEISS_ERROR_INVALID_HANDLE)
    return 9;

  gneiss_action_state action_state = GNEISS_ACTION_STATE_INIT;
  if (gneiss::engine::result{gneiss_game_context_find_action(
          context, "move_horizontal", 15U, &action)} != gneiss::engine::result::success ||
      action == GNEISS_NULL_ACTION ||
      gneiss::engine::result{gneiss_game_context_get_action_state(
          context, action, &action_state)} != gneiss::engine::result::success ||
      action_state.held != 0U) {
    return 8;
  }

  // 上下文身份先于动作参数校验；普通 World 查询则先检查输出指针。
  action = 7U;
  gneiss_action_state invalid_action_state = GNEISS_ACTION_STATE_INIT;
  invalid_action_state.struct_size = 0U;
  if (gneiss::engine::result{gneiss_game_context_find_action(context, nullptr, 0U, &action)} !=
          gneiss::engine::result::invalid_argument ||
      action != 7U ||
      gneiss::engine::result{
          gneiss_game_context_find_action(GNEISS_NULL_GAME_CONTEXT, nullptr, 0U, nullptr)} !=
          gneiss::engine::result::invalid_handle ||
      gneiss::engine::result{gneiss_game_context_get_action_state(
          context, 0U, &invalid_action_state)} != gneiss::engine::result::invalid_argument ||
      gneiss::engine::result{gneiss_game_context_get_world(GNEISS_NULL_GAME_CONTEXT, nullptr)} !=
          gneiss::engine::result::invalid_argument) {
    return 6;
  }

  gneiss::engine::result cross_thread_result = gneiss::engine::result::success;
  gneiss::engine::result cross_thread_log_result = gneiss::engine::result::internal;
  gneiss_log_message native_message = GNEISS_LOG_MESSAGE_INIT;
  native_message.category = "test";
  native_message.category_length = 4;
  native_message.message = "worker ready";
  native_message.message_length = 12;
  std::thread other([&] {
    cross_thread_result =
        gneiss::engine::result{gneiss_game_context_get_world(context, &actual_world)};
    cross_thread_log_result =
        gneiss::engine::result{gneiss_game_context_log(context, &native_message)};
  });
  other.join();
  if (cross_thread_result != gneiss::engine::result::invalid_handle ||
      cross_thread_log_result != gneiss::engine::result::success ||
      gneiss::runtime::destroy_game_context(context) != gneiss::engine::result::success ||
      gneiss::engine::result{gneiss_game_context_get_world(context, &actual_world)} !=
          gneiss::engine::result::invalid_handle ||
      gneiss::engine::result{gneiss_game_context_log(context, &native_message)} !=
          gneiss::engine::result::invalid_handle ||
      gneiss::runtime::destroy_game_context(context) != gneiss::engine::result::invalid_handle ||
      native_application->shutdown(application) != gneiss::engine::result::success) {
    return 4;
  }
  if (bridge.reset().failed())
    return 10;
  native_application.reset();
  {
    const std::scoped_lock lock(capture.mutex);
    if (capture.count != 1U || capture.source != "gneiss.test.module" ||
        capture.message != "worker ready") {
      return 5;
    }
  }
  return 0;
}
