// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SRC_GAME_GAME_CONTEXT_INTERNAL_H_
#define GNEISS_SRC_GAME_GAME_CONTEXT_INTERNAL_H_

#include <gneiss/application.h>
#include <gneiss/game_module.h>

#include <string_view>

namespace gneiss::game_internal {

[[nodiscard]] GNEISS_API gneiss_result
create_game_context(gneiss_application application, gneiss_entity_id startup_root_entity,
                    gneiss_game_context* out_context) noexcept;
[[nodiscard]] GNEISS_API gneiss_result destroy_game_context(gneiss_game_context context) noexcept;
[[nodiscard]] GNEISS_API gneiss_result
set_game_context_log_source(gneiss_game_context context, std::string_view source) noexcept;

} // namespace gneiss::game_internal

#endif
