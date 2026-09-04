// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_RUNTIME_IPC_EVENT_H_
#define GNEISS_APPS_EDITOR_RUNTIME_IPC_EVENT_H_

#include "ipc_control_protocol.h"
#include "ipc_data_protocol.h"
#include "ipc_session_protocol.h"

namespace gneiss::editor {

enum class runtime_ipc_event_kind : std::uint8_t {
  hello,
  heartbeat,
  protocol_error,
  shutdown_complete,
  ready,
  state_changed,
  log,
  inspection_snapshot,
  statistics_snapshot,
  property_result,
};

struct runtime_ipc_event final {
  runtime_ipc_event_kind kind = runtime_ipc_event_kind::protocol_error;
  std::uint32_t request_id = 0U;
  ipc_session_hello hello;
  ipc_session_heartbeat heartbeat;
  ipc_session_error error;
  ipc_session_shutdown shutdown;
  ipc_control_state state = ipc_control_state::invalid;
  std::string log;
  ipc_inspection_batch inspection;
  ipc_runtime_statistics statistics;
  ipc_property_write_result property_result;
};

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
