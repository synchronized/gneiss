// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_command_context.h"

#include "outbound/runtime_ipc_outbound.h"

namespace gneiss::runtime_internal {

result runtime_command_context::send_state(ipc_control_state state) noexcept {
  ipc_envelope envelope;
  const auto operation = make_state_event(state, envelope);
  return operation == result::success ? send(std::move(envelope)) : operation;
}

result runtime_command_context::send_protocol_error(result operation, std::string message,
                                                    std::uint32_t request_id) noexcept {
  ipc_envelope envelope;
  const auto encoded =
      make_protocol_error_response(operation, std::move(message), request_id, envelope);
  return encoded == result::success ? send(std::move(envelope)) : encoded;
}

} // namespace gneiss::runtime_internal
