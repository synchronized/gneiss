// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_ipc_command.h"

#include <utility>

namespace gneiss::runtime_internal {

result decode_runtime_session_command(const ipc_envelope& envelope,
                                      runtime_ipc_command& output) noexcept {
  if (envelope.operation == static_cast<std::uint16_t>(ipc_session_operation::hello)) {
    runtime_session_hello_ack command;
    command.request_id = envelope.request_id;
    const auto operation = decode_ipc_session_hello(envelope, command.value);
    if (operation == result::success) {
      output = std::move(command);
    }
    return operation;
  }
  if (envelope.operation == static_cast<std::uint16_t>(ipc_session_operation::heartbeat)) {
    runtime_heartbeat_command command;
    command.request_id = envelope.request_id;
    const auto operation = decode_ipc_session_heartbeat(envelope, command.value);
    if (operation == result::success) {
      output = std::move(command);
    }
    return operation;
  }
  if (envelope.operation == static_cast<std::uint16_t>(ipc_session_operation::protocol_error)) {
    runtime_protocol_error_command command;
    command.request_id = envelope.request_id;
    const auto operation = decode_ipc_session_error(envelope, command.value);
    if (operation == result::success) {
      output = std::move(command);
    }
    return operation;
  }
  return result::unsupported;
}

result decode_runtime_control_command(const ipc_envelope& envelope,
                                      runtime_ipc_command& output) noexcept {
  ipc_control_operation request = ipc_control_operation::invalid;
  const auto operation = decode_ipc_control_request(envelope, request);
  if (operation != result::success) {
    return operation;
  }
  if (request == ipc_control_operation::pause) {
    output = runtime_pause_command{envelope.request_id};
  } else if (request == ipc_control_operation::resume) {
    output = runtime_resume_command{envelope.request_id};
  } else {
    output = runtime_stop_command{envelope.request_id};
  }
  return result::success;
}

result decode_runtime_inspection_command(const ipc_envelope& envelope,
                                         runtime_ipc_command& output) noexcept {
  const auto operation = decode_ipc_inspection_resync(envelope);
  if (operation == result::success) {
    output = runtime_inspection_resync_command{envelope.request_id};
  }
  return operation;
}

result decode_runtime_property_command(const ipc_envelope& envelope,
                                       runtime_ipc_command& output) noexcept {
  runtime_property_write_command command;
  command.request_id = envelope.request_id;
  const auto operation = decode_ipc_property_write_v2(envelope, command.value);
  if (operation == result::success) {
    output = std::move(command);
  }
  return operation;
}

} // namespace gneiss::runtime_internal
