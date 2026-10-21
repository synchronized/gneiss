// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/game_module.h>
#include <gneiss/engine/log.hpp>

#include <cstdint>
#include <memory>
#include <new>
#include <string_view>

namespace {

struct game_state final {
  std::uint64_t update_count{};
};

gneiss::result write_log(gneiss_game_context context, std::string_view message) noexcept {
  const auto value =
      gneiss::to_native(gneiss::make_log_message(gneiss::log_severity::info, "lifecycle", message));
  return gneiss::result{gneiss_game_context_log(context, &value)};
}

gneiss_result initialize(gneiss_game_context context, void** output) noexcept {
  if (output == nullptr)
    return GNEISS_ERROR_INVALID_ARGUMENT;
  std::unique_ptr<game_state> state{new (std::nothrow) game_state};
  if (!state) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  }
  const auto status = write_log(context, "游戏模块初始化完成");
  if (status.ok()) {
    *output = state.release();
  }
  return status.native();
}

gneiss_result fixed_update(gneiss_game_context, void*,
                           const gneiss_game_update_time* time) noexcept {
  return time && time->struct_size >= GNEISS_GAME_UPDATE_TIME_VERSION_1_SIZE && time->reserved == 0
             ? GNEISS_SUCCESS
             : GNEISS_ERROR_INVALID_ARGUMENT;
}

gneiss_result update(gneiss_game_context context, void* module_state,
                     const gneiss_game_update_time* time) noexcept {
  if (fixed_update(context, module_state, time) != GNEISS_SUCCESS)
    return GNEISS_ERROR_INVALID_ARGUMENT;
  if (module_state == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  ++static_cast<game_state*>(module_state)->update_count;
  return GNEISS_SUCCESS;
}

gneiss_result shutdown(gneiss_game_context context, void* module_state) noexcept {
  const auto status = write_log(context, "游戏模块关闭");
  delete static_cast<game_state*>(module_state);
  return status.native();
}

} // namespace

// 模块边界显式使用 C ABI；业务状态仍为原生 C++ 对象。
extern "C" GNEISS_GAME_MODULE_EXPORT gneiss_result
gneiss_game_module_query(uint32_t engine_abi_version, gneiss_game_module_desc* out_desc) {
  if (!out_desc)
    return GNEISS_ERROR_INVALID_ARGUMENT;
  if (engine_abi_version != GNEISS_GAME_MODULE_ABI_VERSION_CURRENT ||
      out_desc->struct_size < sizeof(gneiss_game_module_desc))
    return GNEISS_ERROR_UNSUPPORTED;
  gneiss_game_module_desc desc{};
  desc.struct_size = out_desc->struct_size;
  desc.abi_version = GNEISS_GAME_MODULE_ABI_VERSION_CURRENT;
  constexpr std::string_view id = "gneiss.template.game";
  desc.module_id = id.data();
  desc.module_id_length = id.size();
  desc.initialize = initialize;
  desc.fixed_update = fixed_update;
  desc.update = update;
  desc.shutdown = shutdown;
  const auto status = gneiss_game_module_validate(&desc);
  if (status == GNEISS_SUCCESS)
    *out_desc = desc;
  return status;
}
