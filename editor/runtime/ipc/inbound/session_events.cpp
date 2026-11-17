// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime/ipc/editor_ipc_event.hpp"
#include <gneiss/engine/error.hpp>

#include "runtime/ipc/inbound/event_decode.hpp"

namespace gneiss::host {

gneiss::error decode_runtime_session_event(const gneiss::ipc::envelope& envelope,
                                                   runtime_ipc_event& output) {
  if (envelope.operation == static_cast<std::uint16_t>(ipc_session_operation::hello)) {
    return gneiss::host::detail::decode_value<runtime_hello_event>(
        envelope, output, [&](auto& value) { return decode_ipc_session_hello(envelope, value); });
  }
  if (envelope.operation == static_cast<std::uint16_t>(ipc_session_operation::heartbeat)) {
    return gneiss::host::detail::decode_value<runtime_heartbeat_event>(
        envelope, output,
        [&](auto& value) { return decode_ipc_session_heartbeat(envelope, value); });
  }
  if (envelope.operation == static_cast<std::uint16_t>(ipc_session_operation::protocol_error)) {
    return gneiss::host::detail::decode_value<runtime_protocol_error_event>(
        envelope, output, [&](auto& value) { return decode_ipc_session_error(envelope, value); });
  }
  if (envelope.operation == static_cast<std::uint16_t>(ipc_session_operation::shutdown_complete)) {
    return gneiss::host::detail::decode_value<runtime_shutdown_event>(
        envelope, output,
        [&](auto& value) { return decode_ipc_session_shutdown(envelope, value); });
  }
  return gneiss::error::unsupported;
}

} // namespace gneiss::host
