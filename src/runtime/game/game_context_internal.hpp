// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SRC_GAME_GAME_CONTEXT_INTERNAL_HPP_
#define GNEISS_SRC_GAME_GAME_CONTEXT_INTERNAL_HPP_

#include "engine/core/rid.hpp"
#include <gneiss/engine/game_module.h>

#include "engine/core/result.hpp"
#include <memory>
#include <string_view>

namespace gneiss::engine {
struct message_view;
}

namespace gneiss::runtime {
class application;

[[nodiscard]] GNEISS_API gneiss::engine::result
create_game_context(const std::shared_ptr<application>& parent, std::uint64_t startup_root_entity,
                    gneiss_game_context* out_context) noexcept;
[[nodiscard]] GNEISS_API gneiss::engine::result
destroy_game_context(gneiss_game_context context) noexcept;
[[nodiscard]] GNEISS_API gneiss::engine::result
set_game_context_log_source(gneiss_game_context context, std::string_view source) noexcept;

/** 仅在单次调用期间持有父对象；关闭状态仍需校验，不恢复已关闭服务。 */
struct context_view {
  std::shared_ptr<runtime::application> application;
  gneiss::engine::rid world{};
  std::uint64_t startup_root_entity{};
};
[[nodiscard]] gneiss::engine::result query_context(gneiss_game_context context,
                                                   context_view& output) noexcept;
/** message 已经过 ABI 校验；此入口允许跨线程，日志来源在锁内借用。 */
[[nodiscard]] gneiss::engine::result
submit_context_log(gneiss_game_context context,
                   const gneiss::engine::message_view& message) noexcept;
} // namespace gneiss::runtime

#endif
