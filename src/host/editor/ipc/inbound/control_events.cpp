// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "editor_ipc_event.hpp"
#include "engine/core/error.hpp"

#include "event_decode.hpp"

namespace gneiss::host {

gneiss::engine::error decode_runtime_control_event(const gneiss::engine::ipc_envelope& envelope,
                                                   runtime_ipc_event& output) noexcept {
  if (envelope.operation == static_cast<std::uint16_t>(ipc_control_operation::ready) &&
      envelope.kind == gneiss::engine::ipc_message_kind::event && envelope.request_id == 0U &&
      envelope.payload.empty()) {
    output = runtime_ready_event{};
    return gneiss::engine::error::success;
  }
  return gneiss::host::detail::decode_value<runtime_state_event>(
      envelope, output, [&](auto& value) { return decode_ipc_control_state(envelope, value); });
}

} // namespace gneiss::host
