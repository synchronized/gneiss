// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_RUNTIME_RUNTIME_IPC_COMMAND_H_
#define GNEISS_APPS_RUNTIME_RUNTIME_IPC_COMMAND_H_

#include "ipc_control_protocol.h"
#include "ipc_data_protocol.h"
#include "ipc_session_protocol.h"

#include <variant>

namespace gneiss::runtime_internal {

struct runtime_session_hello_ack final {
  std::uint32_t request_id = 0U;
  ipc_session_hello value;
};
struct runtime_heartbeat_command final {
  std::uint32_t request_id = 0U;
  ipc_session_heartbeat value;
};
struct runtime_protocol_error_command final {
  std::uint32_t request_id = 0U;
  ipc_session_error value;
};
struct runtime_pause_command final {
  std::uint32_t request_id = 0U;
};
struct runtime_resume_command final {
  std::uint32_t request_id = 0U;
};
struct runtime_stop_command final {
  std::uint32_t request_id = 0U;
};
struct runtime_inspection_resync_command final {
  std::uint32_t request_id = 0U;
};
struct runtime_property_write_command final {
  std::uint32_t request_id = 0U;
  ipc_property_write value;
};

using runtime_ipc_command =
    std::variant<runtime_session_hello_ack, runtime_heartbeat_command,
                 runtime_protocol_error_command, runtime_pause_command, runtime_resume_command,
                 runtime_stop_command, runtime_inspection_resync_command,
                 runtime_property_write_command>;

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
