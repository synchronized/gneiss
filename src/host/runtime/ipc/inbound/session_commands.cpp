// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "ipc/runtime_commands.hpp"

#include "ipc/outbound/runtime_ipc_outbound.hpp"

#include "ipc_session_protocol.hpp"

namespace gneiss::host::runtime {
namespace {

gneiss::engine::core::result
handle_heartbeat(const gneiss::engine::platform::ipc_envelope& envelope,
                 runtime_command_context& context) noexcept {
  ipc_session_heartbeat heartbeat;
  const auto operation = decode_ipc_session_heartbeat(envelope, heartbeat);
  if (operation != gneiss::engine::core::result::success) {
    return operation;
  }
  gneiss::engine::platform::ipc_envelope response;
  const auto encoded = make_heartbeat_response(heartbeat, envelope.request_id, response);
  return encoded == gneiss::engine::core::result::success ? context.send(std::move(response))
                                                          : encoded;
}

gneiss::engine::core::result
handle_protocol_error(const gneiss::engine::platform::ipc_envelope& envelope,
                      runtime_command_context&) noexcept {
  ipc_session_error error;
  const auto operation = decode_ipc_session_error(envelope, error);
  return operation == gneiss::engine::core::result::success
             ? gneiss::engine::core::result(error.code)
             : operation;
}

} // namespace

gneiss::engine::core::result
register_runtime_session_commands(runtime_command_router& router) noexcept {
  auto operation =
      router.bind(gneiss::engine::platform::ipc_domain::session,
                  static_cast<std::uint16_t>(ipc_session_operation::heartbeat), handle_heartbeat);
  if (operation == gneiss::engine::core::result::success) {
    operation = router.bind(gneiss::engine::platform::ipc_domain::session,
                            static_cast<std::uint16_t>(ipc_session_operation::protocol_error),
                            handle_protocol_error);
  }
  return operation;
}

} // namespace gneiss::host::runtime
