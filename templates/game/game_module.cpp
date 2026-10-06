// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/game_module.hpp>

#include <cstdint>
#include <memory>
#include <new>
#include <string_view>

namespace {

struct game_state final {
  std::uint64_t update_count{};
};

gneiss::result write_log(gneiss::game_context context, std::string_view message) noexcept {
  return context.log(gneiss::make_log_message(gneiss::log_severity::info, "lifecycle", message));
}

gneiss::result initialize(gneiss::game_context context, void*& output) noexcept {
  std::unique_ptr<game_state> state{new (std::nothrow) game_state};
  if (!state) {
    return gneiss::result::out_of_memory;
  }
  const auto status = write_log(context, "游戏模块初始化完成");
  if (status.ok()) {
    output = state.release();
  }
  return status;
}

gneiss::result fixed_update(gneiss::game_context, void*, const gneiss::game_update_time&) noexcept {
  return gneiss::result::success;
}

gneiss::result update(gneiss::game_context, void* module_state,
                      const gneiss::game_update_time&) noexcept {
  if (module_state == nullptr) {
    return gneiss::result::invalid_argument;
  }
  ++static_cast<game_state*>(module_state)->update_count;
  return gneiss::result::success;
}

gneiss::result shutdown(gneiss::game_context context, void* module_state) noexcept {
  const auto status = write_log(context, "游戏模块关闭");
  delete static_cast<game_state*>(module_state);
  return status;
}

using module = gneiss::game_module<gneiss::game_module_callbacks{
    .initialize = initialize,
    .fixed_update = fixed_update,
    .update = update,
    .shutdown = shutdown,
}>;

} // namespace

// 动态库查找的唯一 C ABI 边界；业务回调使用原生 C++ 类型。
extern "C" GNEISS_GAME_MODULE_EXPORT gneiss_result
gneiss_game_module_query(uint32_t engine_abi_version, gneiss_game_module_desc* out_desc) {
  return module::export_query(engine_abi_version, out_desc, {.module_id = "gneiss.template.game"})
      .native();
}
