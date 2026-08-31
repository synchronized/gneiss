// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_GAME_MODULE_HPP_
#define GNEISS_GAME_MODULE_HPP_

#include <gneiss/core/result.hpp>
#include <gneiss/game_module.h>

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
