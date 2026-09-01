// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_GAME_MODULE_HPP_
#define GNEISS_GAME_MODULE_HPP_

#include <gneiss/core/result.hpp>
#include <gneiss/game_module.h>
#include <gneiss/log.hpp>

#include <cstdint>
#include <string_view>

namespace gneiss {

inline constexpr std::uint32_t game_module_abi_version = GNEISS_GAME_MODULE_ABI_VERSION_CURRENT;
inline constexpr std::string_view game_module_query_symbol = GNEISS_GAME_MODULE_QUERY_SYMBOL;

/** 不拥有 Engine Game Context 的强类型句柄。 */
class game_context final {
public:
  constexpr game_context() noexcept = default;
  explicit constexpr game_context(gneiss_game_context value) noexcept : value_(value) {}

  [[nodiscard]] constexpr bool is_valid() const noexcept {
    return value_ != GNEISS_NULL_GAME_CONTEXT;
  }
  [[nodiscard]] constexpr gneiss_game_context get() const noexcept { return value_; }

  [[nodiscard]] result get_world(gneiss_world& out_world) const noexcept {
    return from_native(gneiss_game_context_get_world(value_, &out_world));
  }

  [[nodiscard]] result get_startup_root_entity(gneiss_entity_id& out_entity) const noexcept {
    return from_native(gneiss_game_context_get_startup_root_entity(value_, &out_entity));
  }

  [[nodiscard]] result find_action(std::string_view name,
                                   gneiss_action& out_action) const noexcept {
    return from_native(
        gneiss_game_context_find_action(value_, name.data(), name.size(), &out_action));
  }

  [[nodiscard]] result get_action_state(gneiss_action action,
                                        gneiss_action_state& out_state) const noexcept {
    return from_native(gneiss_game_context_get_action_state(value_, action, &out_state));
  }

  [[nodiscard]] result request_exit() const noexcept {
    return from_native(gneiss_game_context_request_exit(value_));
  }

  [[nodiscard]] result log(const gneiss_log_message& message) const noexcept {
    return from_native(gneiss_game_context_log(value_, &message));
  }

  friend constexpr bool operator==(game_context, game_context) noexcept = default;

private:
  gneiss_game_context value_ = GNEISS_NULL_GAME_CONTEXT;
};

inline constexpr game_context null_game_context{};

/** 校验原生 Game Module 描述，不取得所有权。 */
[[nodiscard]] inline result validate_game_module(const gneiss_game_module_desc& desc) noexcept {
  return from_native(gneiss_game_module_validate(&desc));
}

} // namespace gneiss

#endif
