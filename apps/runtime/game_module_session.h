// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_RUNTIME_GAME_MODULE_SESSION_H_
#define GNEISS_APPS_RUNTIME_GAME_MODULE_SESSION_H_

#include "dynamic_library.h"

#include <gneiss/game_module.h>

#include <filesystem>
#include <string_view>

namespace gneiss {

// Runtime 独占的模块会话；析构顺序固定为关闭模块状态，再卸载动态库。
class game_module_session final {
public:
  game_module_session() noexcept = default;
  ~game_module_session();

  game_module_session(const game_module_session&) = delete;
  game_module_session& operator=(const game_module_session&) = delete;

  [[nodiscard]] result load(const std::filesystem::path& path) noexcept;
  [[nodiscard]] result initialize(gneiss_game_context context) noexcept;
  [[nodiscard]] result fixed_update(const gneiss_game_update_time& time) noexcept;
  [[nodiscard]] result update(const gneiss_game_update_time& time) noexcept;
  [[nodiscard]] result shutdown() noexcept;

  [[nodiscard]] bool is_loaded() const noexcept;
  [[nodiscard]] bool is_initialized() const noexcept;
  [[nodiscard]] std::string_view module_id() const noexcept;

private:
  dynamic_library library_;
  gneiss_game_module_desc desc_{};
  gneiss_game_context context_{};
  void* module_state_{};
  bool initialized_{};
  bool shutdown_called_{};
};

} // namespace gneiss

#endif
