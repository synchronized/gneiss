// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ipc/runtime_commands.hpp"
#include <gneiss/protocol/ipc_session_protocol.hpp>

namespace gneiss::host {
error register_runtime_session_commands(runtime_command_router& router) noexcept {
  auto status = router.bind(session_ping, [](runtime_command_context& context,
                                             protocol::request_context request,
                                             ipc_session_heartbeat heartbeat) {
    auto response = protocol::encode(session_pong, heartbeat, request.id);
    return response ? context.send(std::move(*response)) : response.error().error;
  });
  if (status.failed())
    return status;
  return router.bind(session_editor_error,
                     [](runtime_command_context&, protocol::request_context,
                        ipc_session_error failure) { return error{failure.code}; });
}
} // namespace gneiss::host
