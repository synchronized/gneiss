// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_COMMON_IPC_DOMAINS_CONTROL_IPC_CONTROL_PROTOCOL_H_
#define GNEISS_APPS_COMMON_IPC_DOMAINS_CONTROL_IPC_CONTROL_PROTOCOL_H_

#include "ipc_dispatcher.h"

#include <cstddef>
#include <cstdint>

namespace gneiss {

inline constexpr std::uint16_t ipc_control_domain_version = 1U;
inline constexpr std::size_t ipc_control_max_payload_size = 1U;

enum class ipc_control_operation : std::uint16_t {
  invalid = 0U,
  ready = 1U,
  state_changed = 2U,
  pause = 3U,
  resume = 4U,
  stop = 5U,
};

enum class ipc_control_state : std::uint8_t {
  invalid = 0U,
  loading = 1U,
  ready = 2U,
  running = 3U,
  paused = 4U,
  stopping = 5U,
};

[[nodiscard]] result encode_ipc_control_ready(ipc_envelope& output) noexcept;
[[nodiscard]] result encode_ipc_control_state(ipc_control_state state,
                                              ipc_envelope& output) noexcept;
[[nodiscard]] result decode_ipc_control_state(const ipc_envelope& envelope,
                                              ipc_control_state& output) noexcept;

[[nodiscard]] result encode_ipc_control_request(ipc_control_operation operation,
                                                std::uint32_t request_id,
                                                ipc_envelope& output) noexcept;
[[nodiscard]] result decode_ipc_control_request(const ipc_envelope& envelope,
                                                ipc_control_operation& output) noexcept;

/** 返回 Control 域的固定方向与消息语义规则。 */
[[nodiscard]] std::span<const ipc_operation_descriptor> ipc_control_operations() noexcept;

} // namespace gneiss

#endif
