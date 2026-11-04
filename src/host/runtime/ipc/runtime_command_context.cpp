// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_command_context.hpp"
#include "engine/core/error.hpp"

#include "outbound/runtime_ipc_outbound.hpp"

namespace gneiss::host {

gneiss::engine::error runtime_command_context::send_state(ipc_control_state state) noexcept {
  gneiss::engine::ipc_envelope envelope;
  const auto operation = make_state_event(state, envelope);
  return operation == gneiss::engine::error::success ? send(std::move(envelope)) : operation;
}

gneiss::engine::error
runtime_command_context::send_protocol_error(gneiss::engine::error operation, std::string message,
                                             std::uint32_t request_id) noexcept {
  gneiss::engine::ipc_envelope envelope;
  const auto encoded =
      make_protocol_error_response(operation, std::move(message), request_id, envelope);
  return encoded == gneiss::engine::error::success ? send(std::move(envelope)) : encoded;
}

} // namespace gneiss::host
