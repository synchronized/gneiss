// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ipc/runtime_commands.h"

#include "ipc/outbound/runtime_ipc_outbound.h"

#include "ipc_session_protocol.h"

namespace gneiss::runtime_internal {
namespace {

result handle_heartbeat(const ipc_envelope& envelope, runtime_command_context& context) noexcept {
  ipc_session_heartbeat heartbeat;
  const auto operation = decode_ipc_session_heartbeat(envelope, heartbeat);
  if (operation != result::success) {
    return operation;
  }
  ipc_envelope response;
  const auto encoded = make_heartbeat_response(heartbeat, envelope.request_id, response);
  return encoded == result::success ? context.send(std::move(response)) : encoded;
}

result handle_protocol_error(const ipc_envelope& envelope, runtime_command_context&) noexcept {
  ipc_session_error error;
  const auto operation = decode_ipc_session_error(envelope, error);
  return operation == result::success ? from_native(error.code) : operation;
}

} // namespace

result register_runtime_session_commands(runtime_command_router& router) noexcept {
  auto operation =
      router.bind(ipc_domain::session, static_cast<std::uint16_t>(ipc_session_operation::heartbeat),
                  handle_heartbeat);
  if (operation == result::success) {
    operation = router.bind(ipc_domain::session,
                            static_cast<std::uint16_t>(ipc_session_operation::protocol_error),
                            handle_protocol_error);
  }
  return operation;
}

} // namespace gneiss::runtime_internal
