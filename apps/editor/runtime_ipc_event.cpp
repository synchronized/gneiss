// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_ipc_event.h"

#include <utility>

namespace gneiss::editor {

namespace {

template <typename Decode>
result decode_event(const ipc_envelope& envelope, runtime_ipc_event_kind kind,
                    runtime_ipc_event& output, Decode&& decode) noexcept {
  runtime_ipc_event event;
  event.kind = kind;
  event.request_id = envelope.request_id;
  const auto operation = decode(event);
  if (operation == result::success) {
    output = std::move(event);
  }
  return operation;
}

} // namespace

result decode_runtime_session_event(const ipc_envelope& envelope,
                                    runtime_ipc_event& output) noexcept {
  if (envelope.operation == static_cast<std::uint16_t>(ipc_session_operation::hello)) {
    return decode_event(envelope, runtime_ipc_event_kind::hello, output, [&](auto& event) {
      return decode_ipc_session_hello(envelope, event.hello);
    });
  }
  if (envelope.operation == static_cast<std::uint16_t>(ipc_session_operation::heartbeat)) {
    return decode_event(envelope, runtime_ipc_event_kind::heartbeat, output, [&](auto& event) {
      return decode_ipc_session_heartbeat(envelope, event.heartbeat);
    });
  }
  if (envelope.operation == static_cast<std::uint16_t>(ipc_session_operation::protocol_error)) {
    return decode_event(envelope, runtime_ipc_event_kind::protocol_error, output, [&](auto& event) {
      return decode_ipc_session_error(envelope, event.error);
    });
  }
  if (envelope.operation == static_cast<std::uint16_t>(ipc_session_operation::shutdown_complete)) {
    return decode_event(
        envelope, runtime_ipc_event_kind::shutdown_complete, output,
        [&](auto& event) { return decode_ipc_session_shutdown(envelope, event.shutdown); });
  }
  return result::unsupported;
}

result decode_runtime_control_event(const ipc_envelope& envelope,
                                    runtime_ipc_event& output) noexcept {
  if (envelope.operation == static_cast<std::uint16_t>(ipc_control_operation::ready) &&
      envelope.kind == ipc_message_kind::event && envelope.request_id == 0U &&
      envelope.payload.empty()) {
    runtime_ipc_event event;
    event.kind = runtime_ipc_event_kind::ready;
    output = std::move(event);
    return result::success;
  }
  return decode_event(envelope, runtime_ipc_event_kind::state_changed, output,
                      [&](auto& event) { return decode_ipc_control_state(envelope, event.state); });
}

result decode_runtime_log_event(const ipc_envelope& envelope, runtime_ipc_event& output) noexcept {
  return decode_event(envelope, runtime_ipc_event_kind::log, output,
                      [&](auto& event) { return decode_ipc_log_event(envelope, event.log); });
}

result decode_runtime_inspection_event(const ipc_envelope& envelope,
                                       runtime_ipc_event& output) noexcept {
  return decode_event(
      envelope, runtime_ipc_event_kind::inspection_snapshot, output,
      [&](auto& event) { return decode_ipc_inspection_batch_v2(envelope, event.inspection); });
}

result decode_runtime_statistics_event(const ipc_envelope& envelope,
                                       runtime_ipc_event& output) noexcept {
  return decode_event(
      envelope, runtime_ipc_event_kind::statistics_snapshot, output,
      [&](auto& event) { return decode_ipc_statistics_v2(envelope, event.statistics); });
}

result decode_runtime_property_event(const ipc_envelope& envelope,
                                     runtime_ipc_event& output) noexcept {
  return decode_event(envelope, runtime_ipc_event_kind::property_result, output, [&](auto& event) {
    return decode_ipc_property_result_v2(envelope, event.property_result);
  });
}

} // namespace gneiss::editor
