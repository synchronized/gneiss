// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "editor_ipc_event.h"

#include "event_decode.h"

namespace gneiss::editor {

result decode_runtime_control_event(const ipc_envelope& envelope,
                                    runtime_ipc_event& output) noexcept {
  if (envelope.operation == static_cast<std::uint16_t>(ipc_control_operation::ready) &&
      envelope.kind == ipc_message_kind::event && envelope.request_id == 0U &&
      envelope.payload.empty()) {
    output = runtime_ready_event{};
    return result::success;
  }
  return ipc_internal::decode_value<runtime_state_event>(
      envelope, output, [&](auto& value) { return decode_ipc_control_state(envelope, value); });
}

} // namespace gneiss::editor
