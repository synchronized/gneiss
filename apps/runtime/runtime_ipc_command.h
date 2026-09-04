// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_RUNTIME_RUNTIME_IPC_COMMAND_H_
#define GNEISS_APPS_RUNTIME_RUNTIME_IPC_COMMAND_H_

#include "ipc_control_protocol.h"
#include "ipc_data_protocol.h"
#include "ipc_session_protocol.h"

namespace gneiss::runtime_internal {

enum class runtime_ipc_command_kind : std::uint8_t {
  hello_acknowledgment,
  heartbeat,
  protocol_error,
  pause,
  resume,
  stop,
  inspection_resync,
  property_write,
};

struct runtime_ipc_command final {
  runtime_ipc_command_kind kind = runtime_ipc_command_kind::protocol_error;
  std::uint32_t request_id = 0U;
  ipc_session_hello hello;
  ipc_session_heartbeat heartbeat;
  ipc_session_error error;
  ipc_property_write property;
};

/** 将已通过 Dispatcher 的 Session 域信封解码为 Runtime 主线程命令。 */
[[nodiscard]] result decode_runtime_session_command(const ipc_envelope& envelope,
                                                    runtime_ipc_command& output) noexcept;
/** 将已通过 Dispatcher 的 Control 域信封解码为 Runtime 主线程命令。 */
[[nodiscard]] result decode_runtime_control_command(const ipc_envelope& envelope,
                                                    runtime_ipc_command& output) noexcept;
/** 将已通过 Dispatcher 的 Inspection 域信封解码为 Runtime 主线程命令。 */
[[nodiscard]] result decode_runtime_inspection_command(const ipc_envelope& envelope,
                                                       runtime_ipc_command& output) noexcept;
/** 将已通过 Dispatcher 的 Property 域信封解码为 Runtime 主线程命令。 */
[[nodiscard]] result decode_runtime_property_command(const ipc_envelope& envelope,
                                                     runtime_ipc_command& output) noexcept;

} // namespace gneiss::runtime_internal

#endif
