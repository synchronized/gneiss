// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/game_module.h>

#include <cstdint>
#include <cstring>
#include <new>
#include <string_view>

namespace {

struct game_state final {
  std::uint64_t update_count{};
};

gneiss_result write_log(gneiss_game_context context, std::string_view message) {
  constexpr std::string_view category = "lifecycle";
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
  return gneiss_game_context_log(context, &log_message);
}

gneiss_result initialize(gneiss_game_context context, void** out_state) {
  if (out_state == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  auto* state = new (std::nothrow) game_state;
  if (state == nullptr) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  }
  const auto result = write_log(context, "游戏模块初始化完成");
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

gneiss_result update(gneiss_game_context, void* module_state, const gneiss_game_update_time* time) {
  if (module_state == nullptr || time == nullptr ||
      time->struct_size < GNEISS_GAME_UPDATE_TIME_VERSION_1_SIZE) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  ++static_cast<game_state*>(module_state)->update_count;
  return GNEISS_SUCCESS;
}

gneiss_result shutdown(gneiss_game_context context, void* module_state) {
  const auto result = write_log(context, "游戏模块关闭");
  delete static_cast<game_state*>(module_state);
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
  out_desc->module_id = "gneiss.template.game";
  out_desc->module_id_length = sizeof("gneiss.template.game") - 1U;
  out_desc->initialize = initialize;
  out_desc->fixed_update = fixed_update;
  out_desc->update = update;
  out_desc->shutdown = shutdown;
  return GNEISS_SUCCESS;
}
