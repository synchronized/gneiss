// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SRC_GAME_GAME_CONTEXT_INTERNAL_H_
#define GNEISS_SRC_GAME_GAME_CONTEXT_INTERNAL_H_

#include <gneiss/application.h>
#include <gneiss/game_module.h>

namespace gneiss::game_internal {

GNEISS_API [[nodiscard]] gneiss_result
create_game_context(gneiss_application application, gneiss_entity_id startup_root_entity,
                    gneiss_game_context* out_context) noexcept;
GNEISS_API [[nodiscard]] gneiss_result destroy_game_context(gneiss_game_context context) noexcept;

} // namespace gneiss::game_internal

#endif
