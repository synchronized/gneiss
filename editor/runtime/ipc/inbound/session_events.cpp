// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime/ipc/editor_ipc_event.hpp"
#include <gneiss/engine/core/base/error.hpp>

#include <gneiss/protocol/ipc_session_protocol.hpp>

namespace gneiss::host {

gneiss::error decode_runtime_session_event(const gneiss::ipc::envelope& envelope,
                                           runtime_ipc_event& output) {
  if (envelope.operation == static_cast<std::uint16_t>(ipc_session_operation::hello)) {
    auto decoded = protocol::decode(session_hello, envelope);
    if (!decoded)
      return decoded.error().error;
    output = runtime_hello_event{envelope.request_id, std::move(*decoded)};
    return error::success;
  }
  if (envelope.operation == static_cast<std::uint16_t>(ipc_session_operation::heartbeat)) {
    auto decoded = protocol::decode(session_pong, envelope);
    if (!decoded)
      return decoded.error().error;
    output = runtime_heartbeat_event{envelope.request_id, std::move(*decoded)};
    return error::success;
  }
  if (envelope.operation == static_cast<std::uint16_t>(ipc_session_operation::protocol_error)) {
    auto decoded = protocol::decode(session_runtime_error, envelope);
    if (!decoded)
      return decoded.error().error;
    output = runtime_protocol_error_event{envelope.request_id, std::move(*decoded)};
    return error::success;
  }
  if (envelope.operation == static_cast<std::uint16_t>(ipc_session_operation::shutdown_complete)) {
    auto decoded = protocol::decode(session_shutdown, envelope);
    if (!decoded)
      return decoded.error().error;
    output = runtime_shutdown_event{envelope.request_id, std::move(*decoded)};
    return error::success;
  }
  return gneiss::error::unsupported;
}

} // namespace gneiss::host
