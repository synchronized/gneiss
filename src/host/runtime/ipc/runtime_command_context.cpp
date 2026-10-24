// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_command_context.hpp"
#include "engine/core/result.hpp"

#include "outbound/runtime_ipc_outbound.hpp"

namespace gneiss::host::runtime {

gneiss::engine::core::result runtime_command_context::send_state(ipc_control_state state) noexcept {
  gneiss::engine::platform::ipc_envelope envelope;
  const auto operation = make_state_event(state, envelope);
  return operation == gneiss::engine::core::result::success ? send(std::move(envelope)) : operation;
}

gneiss::engine::core::result
runtime_command_context::send_protocol_error(gneiss::engine::core::result operation,
                                             std::string message,
                                             std::uint32_t request_id) noexcept {
  gneiss::engine::platform::ipc_envelope envelope;
  const auto encoded =
      make_protocol_error_response(operation, std::move(message), request_id, envelope);
  return encoded == gneiss::engine::core::result::success ? send(std::move(envelope)) : encoded;
}

} // namespace gneiss::host::runtime
