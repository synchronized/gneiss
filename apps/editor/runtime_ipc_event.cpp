// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_ipc_event.h"

#include <utility>

namespace gneiss::editor {

result decode_runtime_ipc_event(const ipc_envelope& envelope, runtime_ipc_event& output) noexcept {
  runtime_ipc_event decoded;
  decoded.request_id = envelope.request_id;
  auto operation = result::unsupported;
  if (envelope.domain == ipc_domain::session) {
    if (envelope.operation == static_cast<std::uint16_t>(ipc_session_operation::hello)) {
      decoded.kind = runtime_ipc_event_kind::hello;
      operation = decode_ipc_session_hello(envelope, decoded.hello);
    } else if (envelope.operation == static_cast<std::uint16_t>(ipc_session_operation::heartbeat)) {
      decoded.kind = runtime_ipc_event_kind::heartbeat;
      operation = decode_ipc_session_heartbeat(envelope, decoded.heartbeat);
    } else if (envelope.operation ==
               static_cast<std::uint16_t>(ipc_session_operation::protocol_error)) {
      decoded.kind = runtime_ipc_event_kind::protocol_error;
      operation = decode_ipc_session_error(envelope, decoded.error);
    } else if (envelope.operation ==
               static_cast<std::uint16_t>(ipc_session_operation::shutdown_complete)) {
      decoded.kind = runtime_ipc_event_kind::shutdown_complete;
      operation = decode_ipc_session_shutdown(envelope, decoded.shutdown);
    }
  } else if (envelope.domain == ipc_domain::control) {
    if (envelope.operation == static_cast<std::uint16_t>(ipc_control_operation::ready) &&
        envelope.kind == ipc_message_kind::event && envelope.request_id == 0U &&
        envelope.payload.empty()) {
      decoded.kind = runtime_ipc_event_kind::ready;
      operation = result::success;
    } else {
      decoded.kind = runtime_ipc_event_kind::state_changed;
      operation = decode_ipc_control_state(envelope, decoded.state);
    }
  } else if (envelope.domain == ipc_domain::log) {
    decoded.kind = runtime_ipc_event_kind::log;
    operation = decode_ipc_log_event(envelope, decoded.log);
  } else if (envelope.domain == ipc_domain::inspection) {
    decoded.kind = runtime_ipc_event_kind::inspection_snapshot;
    operation = decode_ipc_inspection_batch_v2(envelope, decoded.inspection);
  } else if (envelope.domain == ipc_domain::statistics) {
    decoded.kind = runtime_ipc_event_kind::statistics_snapshot;
    operation = decode_ipc_statistics_v2(envelope, decoded.statistics);
  } else if (envelope.domain == ipc_domain::property) {
    decoded.kind = runtime_ipc_event_kind::property_result;
    operation = decode_ipc_property_result_v2(envelope, decoded.property_result);
  }
  if (operation == result::success) {
    output = std::move(decoded);
  }
  return operation;
}

} // namespace gneiss::editor
