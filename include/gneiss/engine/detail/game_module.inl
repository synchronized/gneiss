// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_DETAIL_GAME_MODULE_INL_
#define GNEISS_DETAIL_GAME_MODULE_INL_

#include <gneiss/engine/game_module.hpp>

namespace gneiss {

template <game_module_callbacks Callbacks>
gneiss_game_module_desc game_module<Callbacks>::to_native(game_module_desc desc) noexcept {
  gneiss_game_module_desc native{
      .struct_size = sizeof(gneiss_game_module_desc),
      .abi_version = desc.abi_version,
      .module_id = desc.module_id.data(),
      .module_id_length = desc.module_id.size(),
      .initialize = nullptr,
      .fixed_update = nullptr,
      .update = nullptr,
      .shutdown = nullptr,
      .reserved = {},
  };
  // 不实例化缺失回调的桥接，避免编译器生成空函数指针调用。
  if constexpr (Callbacks.initialize != nullptr) {
    native.initialize = initialize;
  }
  if constexpr (Callbacks.fixed_update != nullptr) {
    native.fixed_update = fixed_update;
  }
  if constexpr (Callbacks.update != nullptr) {
    native.update = update;
  }
  if constexpr (Callbacks.shutdown != nullptr) {
    native.shutdown = shutdown;
  }
  return native;
}

template <game_module_callbacks Callbacks>
result game_module<Callbacks>::validate(game_module_desc desc) noexcept {
  return validate_game_module_native(to_native(desc));
}

template <game_module_callbacks Callbacks>
result game_module<Callbacks>::export_query(std::uint32_t engine_abi_version,
                                            gneiss_game_module_desc* output,
                                            game_module_desc desc) noexcept {
  if (output == nullptr) {
    return result::invalid_argument;
  }
  if (engine_abi_version != game_module_abi_version ||
      output->struct_size < sizeof(gneiss_game_module_desc)) {
    return result::unsupported;
  }
  auto native = to_native(desc);
  const auto status = validate_game_module_native(native);
  if (status.ok()) {
    native.struct_size = output->struct_size;
    *output = native;
  }
  return status;
}

template <game_module_callbacks Callbacks>
gneiss_result game_module<Callbacks>::initialize(gneiss_game_context context,
                                                 void** output) noexcept {
  if (output == nullptr) {
    return result::invalid_argument.native();
  }
  void* state = nullptr;
  const auto status = Callbacks.initialize(game_context{context}, state);
  if (status.ok()) {
    *output = state;
  }
  return status.native();
}

template <game_module_callbacks Callbacks>
template <auto Callback>
gneiss_result game_module<Callbacks>::invoke_update(gneiss_game_context context, void* state,
                                                    const gneiss_game_update_time* time) noexcept {
  if (time == nullptr || time->struct_size < GNEISS_GAME_UPDATE_TIME_VERSION_1_SIZE ||
      time->reserved != 0) {
    return result::invalid_argument.native();
  }
  const game_update_time value{
      .update_index = time->update_index,
      .delta_ns = time->delta_ns,
      .elapsed_ns = time->elapsed_ns,
  };
  return Callback(game_context{context}, state, value).native();
}

template <game_module_callbacks Callbacks>
gneiss_result game_module<Callbacks>::fixed_update(gneiss_game_context context, void* state,
                                                   const gneiss_game_update_time* time) noexcept {
  return invoke_update<Callbacks.fixed_update>(context, state, time);
}

template <game_module_callbacks Callbacks>
gneiss_result game_module<Callbacks>::update(gneiss_game_context context, void* state,
                                             const gneiss_game_update_time* time) noexcept {
  return invoke_update<Callbacks.update>(context, state, time);
}

template <game_module_callbacks Callbacks>
gneiss_result game_module<Callbacks>::shutdown(gneiss_game_context context, void* state) noexcept {
  return Callbacks.shutdown(game_context{context}, state).native();
}

} // namespace gneiss

#endif
