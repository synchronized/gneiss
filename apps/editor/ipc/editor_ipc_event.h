// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_IPC_EDITOR_IPC_EVENT_H_
#define GNEISS_APPS_EDITOR_IPC_EDITOR_IPC_EVENT_H_

#include "ipc_control_protocol.h"
#include "ipc_inspection_protocol.h"
#include "ipc_log_protocol.h"
#include "ipc_property_protocol.h"
#include "ipc_session_protocol.h"
#include "ipc_statistics_protocol.h"

#include <variant>

namespace gneiss::editor {

template <typename Value> struct runtime_value_event final {
  std::uint32_t request_id = 0U;
  Value value;
};

using runtime_hello_event = runtime_value_event<ipc_session_hello>;
using runtime_heartbeat_event = runtime_value_event<ipc_session_heartbeat>;
using runtime_protocol_error_event = runtime_value_event<ipc_session_error>;
using runtime_shutdown_event = runtime_value_event<ipc_session_shutdown>;
using runtime_state_event = runtime_value_event<ipc_control_state>;
using runtime_log_event = runtime_value_event<std::string>;
using runtime_inspection_event = runtime_value_event<ipc_inspection_batch>;
using runtime_statistics_event = runtime_value_event<ipc_runtime_statistics>;
using runtime_property_result_event = runtime_value_event<ipc_property_write_result>;
struct runtime_ready_event final {
  std::uint32_t request_id = 0U;
};

using runtime_ipc_event =
    std::variant<runtime_hello_event, runtime_heartbeat_event, runtime_protocol_error_event,
                 runtime_shutdown_event, runtime_ready_event, runtime_state_event,
                 runtime_log_event, runtime_inspection_event, runtime_statistics_event,
                 runtime_property_result_event>;

[[nodiscard]] result decode_runtime_session_event(const ipc_envelope& envelope,
                                                  runtime_ipc_event& output) noexcept;
[[nodiscard]] result decode_runtime_control_event(const ipc_envelope& envelope,
                                                  runtime_ipc_event& output) noexcept;
[[nodiscard]] result decode_runtime_log_event(const ipc_envelope& envelope,
                                              runtime_ipc_event& output) noexcept;
[[nodiscard]] result decode_runtime_inspection_event(const ipc_envelope& envelope,
                                                     runtime_ipc_event& output) noexcept;
[[nodiscard]] result decode_runtime_statistics_event(const ipc_envelope& envelope,
                                                     runtime_ipc_event& output) noexcept;
[[nodiscard]] result decode_runtime_property_event(const ipc_envelope& envelope,
                                                   runtime_ipc_event& output) noexcept;

} // namespace gneiss::editor

#endif
