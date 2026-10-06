// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SRC_GAME_GAME_CONTEXT_INTERNAL_HPP_
#define GNEISS_SRC_GAME_GAME_CONTEXT_INTERNAL_HPP_

#include <gneiss/engine/application.h>
#include <gneiss/engine/game_module.h>

#include <string_view>

namespace gneiss::log_internal {
struct message_view;
}

namespace gneiss::game_internal {

[[nodiscard]] GNEISS_API gneiss_result
create_game_context(gneiss_application application, gneiss_entity_id startup_root_entity,
                    gneiss_game_context* out_context) noexcept;
[[nodiscard]] GNEISS_API gneiss_result destroy_game_context(gneiss_game_context context) noexcept;
[[nodiscard]] GNEISS_API gneiss_result
set_game_context_log_source(gneiss_game_context context, std::string_view source) noexcept;

/** 非拥有型快照；只复制身份，不延长 Application 或 World 的生命周期。 */
struct context_view {
  gneiss_application application{};
  gneiss_world world{};
  gneiss_entity_id startup_root_entity{};
};
[[nodiscard]] gneiss_result query_context(gneiss_game_context context,
                                          context_view& output) noexcept;
/** message 已经过 ABI 校验；此入口允许跨线程，日志来源在锁内借用。 */
[[nodiscard]] gneiss_result submit_context_log(gneiss_game_context context,
                                               const log_internal::message_view& message) noexcept;
} // namespace gneiss::game_internal

#endif
