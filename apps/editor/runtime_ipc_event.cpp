// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_ipc_event.h"

#include <utility>

namespace gneiss::editor {

namespace {

template <typename Event, typename Decode>
result decode_value(const ipc_envelope& envelope, runtime_ipc_event& output,
                    Decode&& decode) noexcept {
  Event event;
  event.request_id = envelope.request_id;
  const auto operation = decode(event.value);
  if (operation == result::success) {
    output = std::move(event);
  }
  return operation;
}

} // namespace

result decode_runtime_session_event(const ipc_envelope& envelope,
                                    runtime_ipc_event& output) noexcept {
  if (envelope.operation == static_cast<std::uint16_t>(ipc_session_operation::hello)) {
    return decode_value<runtime_hello_event>(
        envelope, output, [&](auto& value) { return decode_ipc_session_hello(envelope, value); });
  }
  if (envelope.operation == static_cast<std::uint16_t>(ipc_session_operation::heartbeat)) {
    return decode_value<runtime_heartbeat_event>(envelope, output, [&](auto& value) {
      return decode_ipc_session_heartbeat(envelope, value);
    });
  }
  if (envelope.operation == static_cast<std::uint16_t>(ipc_session_operation::protocol_error)) {
    return decode_value<runtime_protocol_error_event>(
        envelope, output, [&](auto& value) { return decode_ipc_session_error(envelope, value); });
  }
  if (envelope.operation == static_cast<std::uint16_t>(ipc_session_operation::shutdown_complete)) {
    return decode_value<runtime_shutdown_event>(envelope, output, [&](auto& value) {
      return decode_ipc_session_shutdown(envelope, value);
    });
  }
  return result::unsupported;
}

result decode_runtime_control_event(const ipc_envelope& envelope,
                                    runtime_ipc_event& output) noexcept {
  if (envelope.operation == static_cast<std::uint16_t>(ipc_control_operation::ready) &&
      envelope.kind == ipc_message_kind::event && envelope.request_id == 0U &&
      envelope.payload.empty()) {
    output = runtime_ready_event{};
    return result::success;
  }
  return decode_value<runtime_state_event>(
      envelope, output, [&](auto& value) { return decode_ipc_control_state(envelope, value); });
}

result decode_runtime_log_event(const ipc_envelope& envelope, runtime_ipc_event& output) noexcept {
  return decode_value<runtime_log_event>(
      envelope, output, [&](auto& value) { return decode_ipc_log_event(envelope, value); });
}

result decode_runtime_inspection_event(const ipc_envelope& envelope,
                                       runtime_ipc_event& output) noexcept {
  return decode_value<runtime_inspection_event>(envelope, output, [&](auto& value) {
    return decode_ipc_inspection_batch_v2(envelope, value);
  });
}

result decode_runtime_statistics_event(const ipc_envelope& envelope,
                                       runtime_ipc_event& output) noexcept {
  return decode_value<runtime_statistics_event>(
      envelope, output, [&](auto& value) { return decode_ipc_statistics_v2(envelope, value); });
}

result decode_runtime_property_event(const ipc_envelope& envelope,
                                     runtime_ipc_event& output) noexcept {
  return decode_value<runtime_property_result_event>(envelope, output, [&](auto& value) {
    return decode_ipc_property_result_v2(envelope, value);
  });
}

} // namespace gneiss::editor
