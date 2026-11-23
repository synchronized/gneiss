// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ipc/runtime_command_context.hpp"
#include <gneiss/engine/core/base/error.hpp>

#include "ipc/outbound/runtime_ipc_outbound.hpp"

namespace gneiss::host {

gneiss::error runtime_command_context::send_state(ipc_control_state state) {
  auto envelope = protocol::encode(control_state_changed, ipc_control_state_message{state});
  return envelope ? send(std::move(*envelope)) : envelope.error().error;
}

gneiss::error runtime_command_context::send_protocol_error(gneiss::error operation,
                                                           std::string message,
                                                           std::uint32_t request_id) {
  auto encoded = protocol::encode(
      session_runtime_error,
      ipc_session_error{.code = operation.code(), .message = std::move(message)}, request_id);
  return encoded ? send(std::move(*encoded)) : encoded.error().error;
}

} // namespace gneiss::host
