// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <cstdint>
#include <gneiss/engine/platform/ipc/envelope.hpp>
namespace gneiss::protocol::domain {
inline constexpr std::uint16_t session = 1U;
inline constexpr std::uint16_t control = 2U;
inline constexpr std::uint16_t log = 3U;
inline constexpr std::uint16_t inspection = 4U;
inline constexpr std::uint16_t statistics = 5U;
inline constexpr std::uint16_t property = 6U;
inline constexpr std::uint16_t asset = 7U;
inline constexpr std::uint16_t scene = 8U;
}

namespace gneiss::protocol {
/** 两端使用同一份本地策略，不接受对端声明优先级。 */
inline bool is_discardable(const ipc::envelope& message) noexcept {
  return message.domain == domain::log && message.kind == ipc::message_kind::event;
}
}
