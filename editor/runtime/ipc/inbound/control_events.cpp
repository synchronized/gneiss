// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime/ipc/editor_ipc_event.hpp"
#include <gneiss/engine/error.hpp>

namespace gneiss::host {

gneiss::error decode_runtime_control_event(const gneiss::ipc::envelope& envelope,
                                           runtime_ipc_event& output) noexcept {
  if (envelope.operation == static_cast<std::uint16_t>(ipc_control_operation::ready) &&
      envelope.kind == gneiss::ipc::message_kind::event && envelope.request_id == 0U &&
      envelope.payload.empty()) {
    output = runtime_ready_event{};
    return gneiss::error::success;
  }
  auto decoded = protocol::decode(control_state_changed, envelope);
  if (!decoded)
    return decoded.error().error;
  output = runtime_state_event{envelope.request_id, decoded->state};
  return error::success;
}

} // namespace gneiss::host
