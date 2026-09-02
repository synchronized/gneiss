// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/game_module.h>
#include <gneiss/scene.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>
#include <string>
#include <string_view>

namespace {

struct lantern_game_state final {
  gneiss_world world = GNEISS_NULL_WORLD;
  gneiss_entity_id pivot = GNEISS_NULL_ENTITY_ID;
  gneiss_action rotate = GNEISS_NULL_ACTION;
  gneiss_transform transform = GNEISS_TRANSFORM_IDENTITY;
  float angle{};
  std::uint64_t update_count{};
};

gneiss_result log_runtime_progress(gneiss_game_context context, std::uint64_t update_count) {
  constexpr std::string_view category = "runtime_progress";
  const auto message = "Lantern Gallery 运行帧=" + std::to_string(update_count);
  const gneiss_log_message log_message = {
      .struct_size = sizeof(gneiss_log_message),
      .severity = GNEISS_LOG_DEBUG,
      .category = category.data(),
      .category_length = category.size(),
      .message = message.data(),
      .message_length = message.size(),
      .result = GNEISS_SUCCESS,
      .flags = 0U,
      .reserved = {},
  };
  return gneiss_game_context_log(context, &log_message);
}

gneiss_result initialize(gneiss_game_context context, void** out_state) {
  if (out_state == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  auto* state = new (std::nothrow) lantern_game_state;
  if (state == nullptr) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  }
  auto result = gneiss_game_context_get_world(context, &state->world);
  if (result == GNEISS_SUCCESS) {
    result = gneiss_game_context_get_startup_root_entity(context, &state->pivot);
  }
  constexpr char action_name[] = "move_horizontal";
  if (result == GNEISS_SUCCESS) {
    result = gneiss_game_context_find_action(context, action_name, sizeof(action_name) - 1U,
                                             &state->rotate);
  }
  if (result == GNEISS_SUCCESS) {
    result = gneiss_world_entity_get_local_transform(state->world, state->pivot, &state->transform);
  }
  if (result != GNEISS_SUCCESS) {
    delete state;
    return result;
  }
  constexpr std::string_view category = "lifecycle";
  constexpr std::string_view message = "Lantern Gallery 游戏模块初始化完成";
  const gneiss_log_message log_message = {
      .struct_size = sizeof(gneiss_log_message),
      .severity = GNEISS_LOG_INFO,
      .category = category.data(),
      .category_length = category.size(),
      .message = message.data(),
      .message_length = message.size(),
      .result = GNEISS_SUCCESS,
      .flags = 0U,
      .reserved = {},
  };
  result = gneiss_game_context_log(context, &log_message);
  if (result != GNEISS_SUCCESS) {
    delete state;
    return result;
  }
  *out_state = state;
  return GNEISS_SUCCESS;
}

gneiss_result fixed_update(gneiss_game_context, void*, const gneiss_game_update_time*) {
  return GNEISS_SUCCESS;
}

gneiss_result update(gneiss_game_context context, void* module_state,
                     const gneiss_game_update_time* time) {
  if (module_state == nullptr || time == nullptr ||
      time->struct_size < GNEISS_GAME_UPDATE_TIME_VERSION_1_SIZE) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  auto& state = *static_cast<lantern_game_state*>(module_state);
  ++state.update_count;
  constexpr std::uint64_t progress_interval = 30U;
  if (state.update_count % progress_interval == 0U) {
    const auto progress_result = log_runtime_progress(context, state.update_count);
    if (progress_result != GNEISS_SUCCESS) {
      return progress_result;
    }
  }
  gneiss_action_state action = GNEISS_ACTION_STATE_INIT;
  auto result = gneiss_game_context_get_action_state(context, state.rotate, &action);
  if (result != GNEISS_SUCCESS) {
    return result;
  }
  constexpr float idle_radians_per_second = 0.25F;
  constexpr float input_radians_per_second = 1.25F;
  constexpr double nanoseconds_per_second = 1'000'000'000.0;
  state.angle += (idle_radians_per_second + (action.value * input_radians_per_second)) *
                 static_cast<float>(static_cast<double>(time->delta_ns) / nanoseconds_per_second);
  const auto half_angle = state.angle * 0.5F;
  state.transform.rotation[0] = 0.0F;
  state.transform.rotation[1] = std::sin(half_angle);
  state.transform.rotation[2] = 0.0F;
  state.transform.rotation[3] = std::cos(half_angle);
  return gneiss_world_entity_set_local_transform(state.world, state.pivot, &state.transform);
}

gneiss_result shutdown(gneiss_game_context context, void* module_state) {
  constexpr std::string_view category = "lifecycle";
  constexpr std::string_view message = "Lantern Gallery 游戏模块关闭";
  const gneiss_log_message log_message = {
      .struct_size = sizeof(gneiss_log_message),
      .severity = GNEISS_LOG_INFO,
      .category = category.data(),
      .category_length = category.size(),
      .message = message.data(),
      .message_length = message.size(),
      .result = GNEISS_SUCCESS,
      .flags = 0U,
      .reserved = {},
  };
  const auto result = gneiss_game_context_log(context, &log_message);
  delete static_cast<lantern_game_state*>(module_state);
  return result;
}

} // namespace

extern "C" GNEISS_GAME_MODULE_EXPORT gneiss_result
gneiss_game_module_query(uint32_t engine_abi_version, gneiss_game_module_desc* out_desc) {
  if (engine_abi_version != GNEISS_GAME_MODULE_ABI_VERSION_CURRENT || out_desc == nullptr ||
      out_desc->struct_size < GNEISS_GAME_MODULE_DESC_VERSION_1_SIZE) {
    return GNEISS_ERROR_UNSUPPORTED;
  }
  const auto size = out_desc->struct_size;
  std::memset(out_desc, 0, size);
  out_desc->struct_size = size;
  out_desc->abi_version = GNEISS_GAME_MODULE_ABI_VERSION_CURRENT;
  out_desc->module_id = "gneiss.examples.lantern_gallery";
  out_desc->module_id_length = sizeof("gneiss.examples.lantern_gallery") - 1U;
  out_desc->initialize = initialize;
  out_desc->fixed_update = fixed_update;
  out_desc->update = update;
  out_desc->shutdown = shutdown;
  return GNEISS_SUCCESS;
}
