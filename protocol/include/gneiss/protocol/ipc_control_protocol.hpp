// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_COMMON_IPC_DOMAINS_CONTROL_IPC_CONTROL_PROTOCOL_H_
#define GNEISS_APPS_COMMON_IPC_DOMAINS_CONTROL_IPC_CONTROL_PROTOCOL_H_

#include <gneiss/engine/error.hpp>
#include <gneiss/protocol/message.hpp>

#include <cstddef>
#include <cstdint>

namespace gneiss::host {

inline constexpr std::uint16_t ipc_control_domain_version = 2U;
inline constexpr std::size_t ipc_control_max_payload_size = 1024U;

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

/** Control v2 状态通知；请求仍由帧头表示，负载为空。 */
struct ipc_control_state_message final {
  ipc_control_state state{ipc_control_state::invalid};
};

extern const protocol::message_descriptor<ipc_control_state_message> control_state_changed;

[[nodiscard]] gneiss::error encode_ipc_control_ready(gneiss::ipc::envelope& output) noexcept;

[[nodiscard]] gneiss::error encode_ipc_control_request(ipc_control_operation operation,
                                                       std::uint32_t request_id,
                                                       gneiss::ipc::envelope& output) noexcept;
[[nodiscard]] gneiss::error decode_ipc_control_request(const gneiss::ipc::envelope& envelope,
                                                       ipc_control_operation& output) noexcept;

/** 返回 Control 域的固定方向与消息语义规则。 */
[[nodiscard]] std::span<const gneiss::protocol::operation_descriptor>
ipc_control_operations() noexcept;

} // namespace gneiss::host

#endif
