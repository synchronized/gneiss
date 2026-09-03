// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_ipc_session.h"

#include "ipc_control_protocol.h"
#include "ipc_data_protocol.h"
#include "ipc_session_protocol.h"

#include <chrono>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;

bool poll_envelope(gneiss::ipc_transport& transport, gneiss::ipc_envelope& output) {
  std::vector<gneiss::ipc_transport_event> events;
  (void)transport.poll_events(events);
  for (auto& event : events) {
    if (event.type == gneiss::ipc_transport_event_type::envelope_received) {
      output = std::move(event.envelope);
      return true;
    }
  }
  return false;
}

bool send_control(gneiss::ipc_transport& transport, gneiss::ipc_control_operation operation,
                  std::uint32_t request_id) {
  gneiss::ipc_envelope envelope;
  return gneiss::encode_ipc_control_request(operation, request_id, envelope) ==
             gneiss::result::success &&
         transport.send(envelope) == gneiss::result::success;
}

bool wait_for_action(gneiss::runtime_internal::runtime_ipc_session& session,
                     gneiss::runtime_internal::runtime_ipc_actions& actions,
                     bool gneiss::runtime_internal::runtime_ipc_actions::* member) {
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline) {
    if (session.pump(std::chrono::steady_clock::now(), actions) != gneiss::result::success) {
      return false;
    }
    if (actions.*member) {
      return true;
    }
    std::this_thread::sleep_for(1ms);
  }
  return false;
}

bool complete_handshake(gneiss::ipc_transport& server,
                        gneiss::runtime_internal::runtime_ipc_session& session,
                        std::span<const gneiss::ipc_domain_capability> supported =
                            gneiss::ipc_v2_domain_capabilities()) {
  gneiss::runtime_internal::runtime_ipc_actions actions;
  gneiss::ipc_envelope hello;
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline && !poll_envelope(server, hello)) {
    if (session.pump(std::chrono::steady_clock::now(), actions) != gneiss::result::success) {
      return false;
    }
    std::this_thread::sleep_for(1ms);
  }
  gneiss::ipc_session_hello request;
  gneiss::ipc_session_hello acknowledgment_message;
  gneiss::ipc_envelope acknowledgment;
  if (gneiss::decode_ipc_session_hello(hello, request) != gneiss::result::success ||
      gneiss::negotiate_ipc_session_hello(request, "secret", supported, acknowledgment_message) !=
          gneiss::result::success ||
      gneiss::encode_ipc_session_hello(acknowledgment_message, true, hello.request_id,
                                       acknowledgment) != gneiss::result::success ||
      server.send(acknowledgment) != gneiss::result::success) {
    return false;
  }
  const auto authenticated_deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < authenticated_deadline) {
    if (session.pump(std::chrono::steady_clock::now(), actions) != gneiss::result::success) {
      return false;
    }
    if (session.state() == gneiss::runtime_internal::runtime_ipc_state::running) {
      return true;
    }
    std::this_thread::sleep_for(1ms);
  }
  return false;
}

bool test_property_write_round_trip() {
  gneiss::ipc_transport server;
  if (server.start_server(gneiss::ipc_transport_protocol::envelope_v2) != gneiss::result::success) {
    return false;
  }
  gneiss::runtime_internal::runtime_ipc_session session(
      {{"127.0.0.1", server.endpoint().port}, "secret", 3s, 3s});
  const auto supported = gneiss::ipc_v2_domain_capabilities();
  if (session.start(std::chrono::steady_clock::now()) != gneiss::result::success ||
      session.notify_running() != gneiss::result::success ||
      !complete_handshake(server, session, supported)) {
    return false;
  }

  gneiss::ipc_property_write command{.session_id = 7U,
                                     .command_id = 11U,
                                     .object = {2U, 3U},
                                     .type_id = {{1U}},
                                     .field_id = 4U,
                                     .expected_revision = 1U,
                                     .value = {std::array<float, 3>{1.0F, 2.0F, 3.0F}}};
  gneiss::ipc_envelope frame;
  if (gneiss::encode_ipc_property_write_v2(command, 11U, frame) != gneiss::result::success ||
      server.send(frame) != gneiss::result::success) {
    return false;
  }
  gneiss::runtime_internal::runtime_ipc_actions actions;
  const auto command_deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < command_deadline && actions.property_writes.empty()) {
    if (session.pump(std::chrono::steady_clock::now(), actions) != gneiss::result::success) {
      return false;
    }
    std::this_thread::sleep_for(1ms);
  }
  if (actions.property_writes.size() != 1U ||
      actions.property_writes.front().command_id != command.command_id) {
    return false;
  }

  const gneiss::ipc_property_write_result response{.session_id = 7U,
                                                   .command_id = 11U,
                                                   .code = GNEISS_SUCCESS,
                                                   .revision = 2U,
                                                   .message = "属性已应用",
                                                   .canonical_value = command.value};
  if (session.notify_property_write_result(response) != gneiss::result::success) {
    return false;
  }
  const auto response_deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < response_deadline) {
    if (poll_envelope(server, frame)) {
      gneiss::ipc_property_write_result decoded;
      if (gneiss::decode_ipc_property_result_v2(frame, decoded) == gneiss::result::success &&
          decoded.command_id == 11U && decoded.revision == 2U) {
        return session.stop() == gneiss::result::success &&
               server.stop() == gneiss::result::success;
      }
    }
    std::this_thread::sleep_for(1ms);
  }
  return false;
}

bool test_property_flood_does_not_block_stop() {
  gneiss::ipc_transport server;
  if (server.start_server(gneiss::ipc_transport_protocol::envelope_v2) != gneiss::result::success) {
    return false;
  }
  gneiss::runtime_internal::runtime_ipc_session session(
      {{"127.0.0.1", server.endpoint().port}, "secret", 3s, 3s});
  const auto supported = gneiss::ipc_v2_domain_capabilities();
  if (session.start(std::chrono::steady_clock::now()) != gneiss::result::success ||
      session.notify_running() != gneiss::result::success ||
      !complete_handshake(server, session, supported)) {
    return false;
  }
  for (std::uint64_t index = 1U; index <= 40U; ++index) {
    const gneiss::ipc_property_write command{.session_id = 7U,
                                             .command_id = index,
                                             .object = {index, 1U},
                                             .type_id = {{1U}},
                                             .field_id = 4U,
                                             .expected_revision = 1U,
                                             .value = {true}};
    gneiss::ipc_envelope frame;
    if (gneiss::encode_ipc_property_write_v2(command, static_cast<std::uint32_t>(index), frame) !=
            gneiss::result::success ||
        server.send(frame) != gneiss::result::success) {
      return false;
    }
  }
  if (!send_control(server, gneiss::ipc_control_operation::stop, 100U)) {
    return false;
  }
  std::this_thread::sleep_for(50ms);
  gneiss::runtime_internal::runtime_ipc_actions actions;
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline && !actions.request_exit) {
    if (session.pump(std::chrono::steady_clock::now(), actions) != gneiss::result::success) {
      return false;
    }
    std::this_thread::sleep_for(1ms);
  }
  return actions.request_exit && actions.property_writes.size() <= 32U &&
         session.state() == gneiss::runtime_internal::runtime_ipc_state::stopping &&
         session.stop() == gneiss::result::success && server.stop() == gneiss::result::success;
}

bool test_control_lifecycle() {
  gneiss::ipc_transport server;
  if (server.start_server(gneiss::ipc_transport_protocol::envelope_v2) != gneiss::result::success) {
    return false;
  }
  gneiss::runtime_internal::runtime_ipc_session session(
      {{"127.0.0.1", server.endpoint().port}, "secret", 3s, 3s});
  const auto now = std::chrono::steady_clock::now();
  if (session.start(now) != gneiss::result::success ||
      session.notify_running() != gneiss::result::success || !complete_handshake(server, session) ||
      !session.game_updates_enabled()) {
    return false;
  }

  constexpr std::string_view source = "test";
  constexpr std::string_view category = "ipc";
  constexpr std::string_view text = "结构化日志";
  const gneiss_log_event log_event = {
      .struct_size = sizeof(gneiss_log_event),
      .severity = GNEISS_LOG_INFO,
      .sequence = 1U,
      .timestamp_ns = 2U,
      .thread_id = 3U,
      .source = source.data(),
      .source_length = source.size(),
      .category = category.data(),
      .category_length = category.size(),
      .message = text.data(),
      .message_length = text.size(),
      .result = GNEISS_SUCCESS,
      .flags = 0U,
      .reserved = {},
  };
  if (session.notify_log_event(log_event) != gneiss::result::success) {
    return false;
  }

  gneiss::runtime_internal::runtime_ipc_actions actions;
  gneiss::ipc_envelope resync;
  if (gneiss::encode_ipc_inspection_resync(10U, resync) != gneiss::result::success ||
      server.send(resync) != gneiss::result::success ||
      !wait_for_action(session, actions,
                       &gneiss::runtime_internal::runtime_ipc_actions::request_inspection_resync)) {
    return false;
  }

  if (!send_control(server, gneiss::ipc_control_operation::pause, 11U) ||
      !wait_for_action(session, actions,
                       &gneiss::runtime_internal::runtime_ipc_actions::pause_game) ||
      session.state() != gneiss::runtime_internal::runtime_ipc_state::paused ||
      session.game_updates_enabled()) {
    return false;
  }

  gneiss::ipc_envelope heartbeat;
  if (gneiss::encode_ipc_session_heartbeat({.nonce = 42U}, false, 12U, heartbeat) !=
          gneiss::result::success ||
      server.send(heartbeat) != gneiss::result::success) {
    return false;
  }
  const auto pong_deadline = std::chrono::steady_clock::now() + 3s;
  bool received_pong = false;
  bool received_log = false;
  while (std::chrono::steady_clock::now() < pong_deadline && !received_pong) {
    if (session.pump(std::chrono::steady_clock::now(), actions) != gneiss::result::success) {
      return false;
    }
    std::vector<gneiss::ipc_transport_event> events;
    (void)server.poll_events(events);
    for (const auto& event : events) {
      if (event.type == gneiss::ipc_transport_event_type::envelope_received) {
        gneiss::ipc_session_heartbeat decoded_heartbeat;
        if (gneiss::decode_ipc_session_heartbeat(event.envelope, decoded_heartbeat) ==
                gneiss::result::success &&
            event.envelope.kind == gneiss::ipc_message_kind::response &&
            decoded_heartbeat.nonce == 42U) {
          received_pong = true;
        }
        std::string decoded_log;
        if (gneiss::decode_ipc_log_event(event.envelope, decoded_log) == gneiss::result::success &&
            decoded_log.find("结构化日志") != std::string::npos) {
          received_log = true;
        }
      }
    }
  }
  if (!received_pong || !received_log) {
    return false;
  }

  if (!send_control(server, gneiss::ipc_control_operation::resume, 13U) ||
      !wait_for_action(session, actions,
                       &gneiss::runtime_internal::runtime_ipc_actions::resume_game) ||
      !session.game_updates_enabled()) {
    return false;
  }
  if (!send_control(server, gneiss::ipc_control_operation::stop, 14U) ||
      !wait_for_action(session, actions,
                       &gneiss::runtime_internal::runtime_ipc_actions::request_exit) ||
      session.state() != gneiss::runtime_internal::runtime_ipc_state::stopping) {
    return false;
  }
  return session.stop() == gneiss::result::success && server.stop() == gneiss::result::success;
}

bool test_handshake_timeout() {
  gneiss::ipc_transport server;
  if (server.start_server(gneiss::ipc_transport_protocol::envelope_v2) != gneiss::result::success) {
    return false;
  }
  gneiss::runtime_internal::runtime_ipc_session session(
      {{"127.0.0.1", server.endpoint().port}, "secret", 10ms, 1s});
  const auto start = std::chrono::steady_clock::now();
  gneiss::runtime_internal::runtime_ipc_actions actions;
  if (session.start(start) != gneiss::result::success ||
      session.pump(start + 10ms, actions) != gneiss::result::not_ready || !actions.request_exit ||
      actions.failure != gneiss::result::not_ready ||
      session.state() != gneiss::runtime_internal::runtime_ipc_state::failed) {
    return false;
  }
  return session.stop() == gneiss::result::success && server.stop() == gneiss::result::success;
}

bool test_heartbeat_timeout() {
  gneiss::ipc_transport server;
  if (server.start_server(gneiss::ipc_transport_protocol::envelope_v2) != gneiss::result::success) {
    return false;
  }
  gneiss::runtime_internal::runtime_ipc_session session(
      {{"127.0.0.1", server.endpoint().port}, "secret", 3s, 20ms});
  const auto start = std::chrono::steady_clock::now();
  gneiss::runtime_internal::runtime_ipc_actions actions;
  if (session.start(start) != gneiss::result::success ||
      session.notify_running() != gneiss::result::success || !complete_handshake(server, session)) {
    return false;
  }
  const auto timeout_now = std::chrono::steady_clock::now() + 20ms;
  return session.pump(timeout_now, actions) == gneiss::result::not_ready && actions.request_exit &&
         session.state() == gneiss::runtime_internal::runtime_ipc_state::failed &&
         session.stop() == gneiss::result::success && server.stop() == gneiss::result::success;
}

bool test_disconnect() {
  gneiss::ipc_transport server;
  if (server.start_server(gneiss::ipc_transport_protocol::envelope_v2) != gneiss::result::success) {
    return false;
  }
  gneiss::runtime_internal::runtime_ipc_session disconnected(
      {{"127.0.0.1", server.endpoint().port}, "secret", 3s, 3s});
  if (disconnected.start(std::chrono::steady_clock::now()) != gneiss::result::success ||
      disconnected.notify_running() != gneiss::result::success ||
      !complete_handshake(server, disconnected) || server.stop() != gneiss::result::success) {
    return false;
  }
  gneiss::runtime_internal::runtime_ipc_actions actions;
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline) {
    const auto operation = disconnected.pump(std::chrono::steady_clock::now(), actions);
    if (actions.request_exit) {
      return operation != gneiss::result::success &&
             disconnected.state() == gneiss::runtime_internal::runtime_ipc_state::failed &&
             disconnected.stop() == gneiss::result::success;
    }
    std::this_thread::sleep_for(1ms);
  }
  return false;
}

} // namespace

int main() {
  if (!test_property_write_round_trip()) {
    return 1;
  }
  if (!test_control_lifecycle()) {
    return 2;
  }
  if (!test_property_flood_does_not_block_stop()) {
    return 3;
  }
  if (!test_handshake_timeout()) {
    return 4;
  }
  if (!test_heartbeat_timeout()) {
    return 5;
  }
  return test_disconnect() ? 0 : 6;
}
