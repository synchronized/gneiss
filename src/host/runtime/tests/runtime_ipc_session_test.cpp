// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "runtime_ipc_session.hpp"

#include "ipc_control_protocol.hpp"
#include "ipc_inspection_protocol.hpp"
#include "ipc_log_protocol.hpp"
#include "ipc_property_protocol.hpp"
#include "ipc_session_protocol.hpp"

#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

using namespace gneiss::host;

namespace {

using namespace std::chrono_literals;

bool poll_envelope(gneiss::engine::ipc_transport& transport, gneiss::engine::ipc_envelope& output) {
  std::vector<gneiss::engine::ipc_transport_event> events;
  // 每次只取一条，避免返回首个 Envelope 时丢弃同批次中的 Ready 或属性响应。
  (void)transport.poll_events(events, 1U);
  for (auto& event : events) {
    if (event.type == gneiss::engine::ipc_transport_event_type::envelope_received) {
      output = std::move(event.envelope);
      return true;
    }
  }
  return false;
}

bool send_control(gneiss::engine::ipc_transport& transport,
                  gneiss::host::ipc_control_operation operation, std::uint32_t request_id) {
  gneiss::engine::ipc_envelope envelope;
  return gneiss::host::encode_ipc_control_request(operation, request_id, envelope) ==
             gneiss::engine::result::success &&
         transport.send(envelope) == gneiss::engine::result::success;
}

bool wait_for_action(gneiss::host::runtime_ipc_session& session,
                     gneiss::host::runtime_ipc_actions& actions,
                     bool gneiss::host::runtime_ipc_actions::* member) {
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline) {
    if (session.pump(std::chrono::steady_clock::now(), actions) !=
        gneiss::engine::result::success) {
      return false;
    }
    if (actions.*member) {
      return true;
    }
    std::this_thread::sleep_for(1ms);
  }
  return false;
}

bool complete_handshake(gneiss::engine::ipc_transport& server,
                        gneiss::host::runtime_ipc_session& session,
                        std::span<const gneiss::engine::ipc_domain_capability> supported =
                            gneiss::host::ipc_v2_domain_capabilities()) {
  gneiss::host::runtime_ipc_actions actions;
  gneiss::engine::ipc_envelope hello;
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline && !poll_envelope(server, hello)) {
    if (session.pump(std::chrono::steady_clock::now(), actions) !=
        gneiss::engine::result::success) {
      return false;
    }
    std::this_thread::sleep_for(1ms);
  }
  gneiss::host::ipc_session_hello request;
  gneiss::host::ipc_session_hello acknowledgment_message;
  gneiss::engine::ipc_envelope acknowledgment;
  if (gneiss::host::decode_ipc_session_hello(hello, request) != gneiss::engine::result::success ||
      gneiss::host::negotiate_ipc_session_hello(request, "secret", supported,
                                                acknowledgment_message) !=
          gneiss::engine::result::success ||
      gneiss::host::encode_ipc_session_hello(acknowledgment_message, true, hello.request_id,
                                             acknowledgment) != gneiss::engine::result::success ||
      server.send(acknowledgment) != gneiss::engine::result::success) {
    return false;
  }
  const auto authenticated_deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < authenticated_deadline) {
    if (session.pump(std::chrono::steady_clock::now(), actions) !=
        gneiss::engine::result::success) {
      return false;
    }
    if (session.state() == gneiss::host::runtime_ipc_state::running) {
      return true;
    }
    std::this_thread::sleep_for(1ms);
  }
  return false;
}

bool test_property_write_round_trip() {
  gneiss::engine::ipc_transport server;
  if (server.start_server() != gneiss::engine::result::success) {
    return false;
  }
  gneiss::host::runtime_ipc_session session(
      {{"127.0.0.1", server.endpoint().port}, "secret", 3s, 3s});
  const auto supported = gneiss::host::ipc_v2_domain_capabilities();
  if (session.start(std::chrono::steady_clock::now()) != gneiss::engine::result::success ||
      session.notify_running() != gneiss::engine::result::success ||
      !complete_handshake(server, session, supported)) {
    return false;
  }

  gneiss::host::ipc_property_write command{
      .session_id = 7U,
      .command_id = 11U,
      .object = {2U, 3U},
      .type_id = gneiss::engine::type_guid{gneiss::engine::guid::from_bytes({1U})},
      .field_id = 4U,
      .expected_revision = 1U,
      .value = {std::array<float, 3>{1.0F, 2.0F, 3.0F}}};
  gneiss::engine::ipc_envelope frame;
  if (gneiss::host::encode_ipc_property_write_v2(command, 11U, frame) !=
          gneiss::engine::result::success ||
      server.send(frame) != gneiss::engine::result::success) {
    return false;
  }
  gneiss::host::runtime_ipc_actions actions;
  const auto command_deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < command_deadline && actions.property_writes.empty()) {
    if (session.pump(std::chrono::steady_clock::now(), actions) !=
        gneiss::engine::result::success) {
      return false;
    }
    std::this_thread::sleep_for(1ms);
  }
  if (actions.property_writes.size() != 1U ||
      actions.property_writes.front().command_id != command.command_id) {
    return false;
  }

  const gneiss::host::ipc_property_write_result response{.session_id = 7U,
                                                         .command_id = 11U,
                                                         .code =
                                                             gneiss::engine::result::success.code(),
                                                         .revision = 2U,
                                                         .message = "属性已应用",
                                                         .canonical_value = command.value};
  if (session.notify_property_write_result(response) != gneiss::engine::result::success) {
    return false;
  }
  const auto response_deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < response_deadline) {
    if (poll_envelope(server, frame)) {
      gneiss::host::ipc_property_write_result decoded;
      if (gneiss::host::decode_ipc_property_result_v2(frame, decoded) ==
              gneiss::engine::result::success &&
          decoded.command_id == 11U && decoded.revision == 2U) {
        return session.stop() == gneiss::engine::result::success &&
               server.stop() == gneiss::engine::result::success;
      }
    }
    std::this_thread::sleep_for(1ms);
  }
  return false;
}

bool test_property_flood_does_not_block_stop() {
  gneiss::engine::ipc_transport server;
  if (server.start_server() != gneiss::engine::result::success) {
    return false;
  }
  gneiss::host::runtime_ipc_session session(
      {{"127.0.0.1", server.endpoint().port}, "secret", 3s, 3s});
  const auto supported = gneiss::host::ipc_v2_domain_capabilities();
  if (session.start(std::chrono::steady_clock::now()) != gneiss::engine::result::success ||
      session.notify_running() != gneiss::engine::result::success ||
      !complete_handshake(server, session, supported)) {
    return false;
  }
  for (std::uint64_t index = 1U; index <= 40U; ++index) {
    const gneiss::host::ipc_property_write command{
        .session_id = 7U,
        .command_id = index,
        .object = {index, 1U},
        .type_id = gneiss::engine::type_guid{gneiss::engine::guid::from_bytes({1U})},
        .field_id = 4U,
        .expected_revision = 1U,
        .value = {true}};
    gneiss::engine::ipc_envelope frame;
    if (gneiss::host::encode_ipc_property_write_v2(command, static_cast<std::uint32_t>(index),
                                                   frame) != gneiss::engine::result::success ||
        server.send(frame) != gneiss::engine::result::success) {
      return false;
    }
  }
  if (!send_control(server, gneiss::host::ipc_control_operation::stop, 100U)) {
    return false;
  }
  std::this_thread::sleep_for(50ms);
  gneiss::host::runtime_ipc_actions actions;
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline && !actions.request_exit) {
    if (session.pump(std::chrono::steady_clock::now(), actions) !=
        gneiss::engine::result::success) {
      return false;
    }
    std::this_thread::sleep_for(1ms);
  }
  return actions.request_exit && actions.property_writes.size() <= 32U &&
         session.state() == gneiss::host::runtime_ipc_state::stopping &&
         session.stop() == gneiss::engine::result::success &&
         server.stop() == gneiss::engine::result::success;
}

bool test_control_lifecycle() {
  gneiss::engine::ipc_transport server;
  if (server.start_server() != gneiss::engine::result::success) {
    return false;
  }
  gneiss::host::runtime_ipc_session session(
      {{"127.0.0.1", server.endpoint().port}, "secret", 3s, 3s});
  const auto now = std::chrono::steady_clock::now();
  if (session.start(now) != gneiss::engine::result::success ||
      session.notify_running() != gneiss::engine::result::success ||
      !complete_handshake(server, session) || !session.game_updates_enabled()) {
    return false;
  }

  constexpr std::string_view source = "test";
  constexpr std::string_view category = "ipc";
  constexpr std::string_view text = "结构化日志";
  const gneiss::engine::event_view log_event{
      .message = {.severity = gneiss::engine::level::info,
                  .source = source,
                  .category = category,
                  .message = text,
                  .result = gneiss::engine::result::success},
      .sequence = 1U,
      .timestamp_ns = 2U,
      .thread_id = 3U,
  };
  if (session.notify_log_event(log_event) != gneiss::engine::result::success) {
    return false;
  }

  gneiss::host::runtime_ipc_actions actions;
  gneiss::engine::ipc_envelope resync;
  if (gneiss::host::encode_ipc_inspection_resync(10U, resync) != gneiss::engine::result::success ||
      server.send(resync) != gneiss::engine::result::success ||
      !wait_for_action(session, actions,
                       &gneiss::host::runtime_ipc_actions::request_inspection_resync)) {
    return false;
  }

  if (!send_control(server, gneiss::host::ipc_control_operation::pause, 11U) ||
      !wait_for_action(session, actions, &gneiss::host::runtime_ipc_actions::pause_game) ||
      session.state() != gneiss::host::runtime_ipc_state::paused ||
      session.game_updates_enabled()) {
    return false;
  }

  gneiss::engine::ipc_envelope heartbeat;
  if (gneiss::host::encode_ipc_session_heartbeat({.nonce = 42U}, false, 12U, heartbeat) !=
          gneiss::engine::result::success ||
      server.send(heartbeat) != gneiss::engine::result::success) {
    return false;
  }
  const auto pong_deadline = std::chrono::steady_clock::now() + 3s;
  bool received_pong = false;
  bool received_log = false;
  while (std::chrono::steady_clock::now() < pong_deadline && (!received_pong || !received_log)) {
    if (session.pump(std::chrono::steady_clock::now(), actions) !=
        gneiss::engine::result::success) {
      return false;
    }
    std::vector<gneiss::engine::ipc_transport_event> events;
    (void)server.poll_events(events);
    for (const auto& event : events) {
      if (event.type == gneiss::engine::ipc_transport_event_type::envelope_received) {
        gneiss::host::ipc_session_heartbeat decoded_heartbeat;
        if (gneiss::host::decode_ipc_session_heartbeat(event.envelope, decoded_heartbeat) ==
                gneiss::engine::result::success &&
            event.envelope.kind == gneiss::engine::ipc_message_kind::response &&
            decoded_heartbeat.nonce == 42U) {
          received_pong = true;
        }
        std::string decoded_log;
        if (gneiss::host::decode_ipc_log_event(event.envelope, decoded_log) ==
                gneiss::engine::result::success &&
            decoded_log.find("结构化日志") != std::string::npos) {
          received_log = true;
        }
      }
    }
  }
  if (!received_pong || !received_log) {
    return false;
  }

  if (!send_control(server, gneiss::host::ipc_control_operation::resume, 13U) ||
      !wait_for_action(session, actions, &gneiss::host::runtime_ipc_actions::resume_game) ||
      !session.game_updates_enabled()) {
    return false;
  }
  if (!send_control(server, gneiss::host::ipc_control_operation::stop, 14U) ||
      !wait_for_action(session, actions, &gneiss::host::runtime_ipc_actions::request_exit) ||
      session.state() != gneiss::host::runtime_ipc_state::stopping) {
    return false;
  }
  return session.stop() == gneiss::engine::result::success &&
         server.stop() == gneiss::engine::result::success;
}

bool test_handshake_timeout() {
  gneiss::engine::ipc_transport server;
  if (server.start_server() != gneiss::engine::result::success) {
    return false;
  }
  gneiss::host::runtime_ipc_session session(
      {{"127.0.0.1", server.endpoint().port}, "secret", 10ms, 1s});
  const auto start = std::chrono::steady_clock::now();
  gneiss::host::runtime_ipc_actions actions;
  if (session.start(start) != gneiss::engine::result::success ||
      session.pump(start + 10ms, actions) != gneiss::engine::result::not_ready ||
      !actions.request_exit || actions.failure != gneiss::engine::result::not_ready ||
      session.state() != gneiss::host::runtime_ipc_state::failed) {
    return false;
  }
  return session.stop() == gneiss::engine::result::success &&
         server.stop() == gneiss::engine::result::success;
}

bool test_heartbeat_timeout() {
  gneiss::engine::ipc_transport server;
  if (server.start_server() != gneiss::engine::result::success) {
    return false;
  }
  gneiss::host::runtime_ipc_session session(
      {{"127.0.0.1", server.endpoint().port}, "secret", 3s, 20ms});
  const auto start = std::chrono::steady_clock::now();
  gneiss::host::runtime_ipc_actions actions;
  if (session.start(start) != gneiss::engine::result::success ||
      session.notify_running() != gneiss::engine::result::success ||
      !complete_handshake(server, session)) {
    return false;
  }
  const auto timeout_now = std::chrono::steady_clock::now() + 20ms;
  return session.pump(timeout_now, actions) == gneiss::engine::result::not_ready &&
         actions.request_exit && session.state() == gneiss::host::runtime_ipc_state::failed &&
         session.stop() == gneiss::engine::result::success &&
         server.stop() == gneiss::engine::result::success;
}

bool test_disconnect() {
  gneiss::engine::ipc_transport server;
  if (server.start_server() != gneiss::engine::result::success) {
    return false;
  }
  gneiss::host::runtime_ipc_session disconnected(
      {{"127.0.0.1", server.endpoint().port}, "secret", 3s, 3s});
  if (disconnected.start(std::chrono::steady_clock::now()) != gneiss::engine::result::success ||
      disconnected.notify_running() != gneiss::engine::result::success ||
      !complete_handshake(server, disconnected) ||
      server.stop() != gneiss::engine::result::success) {
    return false;
  }
  gneiss::host::runtime_ipc_actions actions;
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline) {
    const auto operation = disconnected.pump(std::chrono::steady_clock::now(), actions);
    if (actions.request_exit) {
      return operation != gneiss::engine::result::success &&
             disconnected.state() == gneiss::host::runtime_ipc_state::failed &&
             disconnected.stop() == gneiss::engine::result::success;
    }
    std::this_thread::sleep_for(1ms);
  }
  return false;
}

} // namespace

int main() {
  if (!test_property_write_round_trip()) {
    std::fputs("IPC 测试失败：属性写入往返\n", stderr);
    return 1;
  }
  if (!test_control_lifecycle()) {
    std::fputs("IPC 测试失败：控制生命周期\n", stderr);
    return 2;
  }
  if (!test_property_flood_does_not_block_stop()) {
    std::fputs("IPC 测试失败：属性洪泛下停止\n", stderr);
    return 3;
  }
  if (!test_handshake_timeout()) {
    std::fputs("IPC 测试失败：握手超时\n", stderr);
    return 4;
  }
  if (!test_heartbeat_timeout()) {
    std::fputs("IPC 测试失败：心跳超时\n", stderr);
    return 5;
  }
  if (!test_disconnect()) {
    std::fputs("IPC 测试失败：连接断开\n", stderr);
    return 6;
  }
  return 0;
}
