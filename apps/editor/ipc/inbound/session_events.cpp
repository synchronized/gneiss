// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "editor_ipc_event.h"

#include "event_decode.h"

namespace gneiss::editor {

result decode_runtime_session_event(const ipc_envelope& envelope,
                                    runtime_ipc_event& output) noexcept {
  if (envelope.operation == static_cast<std::uint16_t>(ipc_session_operation::hello)) {
    return ipc_internal::decode_value<runtime_hello_event>(
        envelope, output, [&](auto& value) { return decode_ipc_session_hello(envelope, value); });
  }
  if (envelope.operation == static_cast<std::uint16_t>(ipc_session_operation::heartbeat)) {
    return ipc_internal::decode_value<runtime_heartbeat_event>(envelope, output, [&](auto& value) {
      return decode_ipc_session_heartbeat(envelope, value);
    });
  }
  if (envelope.operation == static_cast<std::uint16_t>(ipc_session_operation::protocol_error)) {
    return ipc_internal::decode_value<runtime_protocol_error_event>(
        envelope, output, [&](auto& value) { return decode_ipc_session_error(envelope, value); });
  }
  if (envelope.operation == static_cast<std::uint16_t>(ipc_session_operation::shutdown_complete)) {
    return ipc_internal::decode_value<runtime_shutdown_event>(envelope, output, [&](auto& value) {
      return decode_ipc_session_shutdown(envelope, value);
    });
  }
  return result::unsupported;
}

} // namespace gneiss::editor
