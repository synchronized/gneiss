// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_ipc_command.h"

#include <utility>

namespace gneiss::runtime_internal {

result decode_runtime_ipc_command(const ipc_envelope& envelope,
                                  runtime_ipc_command& output) noexcept {
  runtime_ipc_command decoded;
  decoded.request_id = envelope.request_id;
  result operation = result::unsupported;
  if (envelope.domain == ipc_domain::session) {
    if (envelope.operation == static_cast<std::uint16_t>(ipc_session_operation::hello)) {
      decoded.kind = runtime_ipc_command_kind::hello_acknowledgment;
      operation = decode_ipc_session_hello(envelope, decoded.hello);
    } else if (envelope.operation == static_cast<std::uint16_t>(ipc_session_operation::heartbeat)) {
      decoded.kind = runtime_ipc_command_kind::heartbeat;
      operation = decode_ipc_session_heartbeat(envelope, decoded.heartbeat);
    } else if (envelope.operation ==
               static_cast<std::uint16_t>(ipc_session_operation::protocol_error)) {
      decoded.kind = runtime_ipc_command_kind::protocol_error;
      operation = decode_ipc_session_error(envelope, decoded.error);
    }
  } else if (envelope.domain == ipc_domain::control) {
    ipc_control_operation request = ipc_control_operation::invalid;
    operation = decode_ipc_control_request(envelope, request);
    if (operation == result::success) {
      if (request == ipc_control_operation::pause) {
        decoded.kind = runtime_ipc_command_kind::pause;
      } else if (request == ipc_control_operation::resume) {
        decoded.kind = runtime_ipc_command_kind::resume;
      } else {
        decoded.kind = runtime_ipc_command_kind::stop;
      }
    }
  } else if (envelope.domain == ipc_domain::inspection) {
    decoded.kind = runtime_ipc_command_kind::inspection_resync;
    operation = decode_ipc_inspection_resync(envelope);
  } else if (envelope.domain == ipc_domain::property) {
    decoded.kind = runtime_ipc_command_kind::property_write;
    operation = decode_ipc_property_write_v2(envelope, decoded.property);
  }
  if (operation == result::success) {
    output = std::move(decoded);
  }
  return operation;
}

} // namespace gneiss::runtime_internal
