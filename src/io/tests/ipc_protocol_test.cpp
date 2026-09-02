// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ipc_protocol.h"
#include "ipc_transport.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace {

bool round_trip(const gneiss::ipc_message& source, gneiss::ipc_message& decoded) {
  gneiss::ipc_frame frame;
  return gneiss::encode_ipc_message(source, frame) == gneiss::result::success &&
         frame.protocol_major == gneiss::ipc_protocol_major &&
         frame.protocol_minor == gneiss::ipc_protocol_minor &&
         frame.message_type == static_cast<std::uint16_t>(source.type) &&
         gneiss::decode_ipc_message(frame, decoded) == gneiss::result::success &&
         decoded.type == source.type;
}

bool test_all_message_types() {
  gneiss::ipc_message source;
  gneiss::ipc_message decoded;

  source.type = gneiss::ipc_message_type::hello;
  source.session_token = "session-token";
  source.capabilities = {"control", "logs"};
  if (!round_trip(source, decoded) || decoded.session_token != source.session_token ||
      decoded.capabilities != source.capabilities) {
    return false;
  }

  source = {};
  source.type = gneiss::ipc_message_type::hello_ack;
  source.capabilities = {"control"};
  if (!round_trip(source, decoded) || decoded.capabilities != source.capabilities) {
    return false;
  }

  for (const auto type : {gneiss::ipc_message_type::ready, gneiss::ipc_message_type::pause,
                          gneiss::ipc_message_type::resume, gneiss::ipc_message_type::stop}) {
    source = {};
    source.type = type;
    if (!round_trip(source, decoded)) {
      return false;
    }
  }

  for (const auto state : {gneiss::ipc_runtime_state::loading, gneiss::ipc_runtime_state::ready,
                           gneiss::ipc_runtime_state::running, gneiss::ipc_runtime_state::paused,
                           gneiss::ipc_runtime_state::stopping}) {
    source = {};
    source.type = gneiss::ipc_message_type::state_changed;
    source.runtime_state = state;
    if (!round_trip(source, decoded) || decoded.runtime_state != state) {
      return false;
    }
  }

  source = {};
  source.type = gneiss::ipc_message_type::log_event;
  source.text = R"({"level":"INFO","message":"已启动"})";
  if (!round_trip(source, decoded) || decoded.text != source.text) {
    return false;
  }

  source = {};
  source.type = gneiss::ipc_message_type::error;
  source.code = -9;
  source.text = "operation temporarily not ready";
  if (!round_trip(source, decoded) || decoded.code != source.code || decoded.text != source.text) {
    return false;
  }

  for (const auto type : {gneiss::ipc_message_type::ping, gneiss::ipc_message_type::pong}) {
    source = {};
    source.type = type;
    source.nonce = UINT64_C(0xFEDCBA9876543210);
    if (!round_trip(source, decoded) || decoded.nonce != source.nonce) {
      return false;
    }
  }

  source = {};
  source.type = gneiss::ipc_message_type::shutdown_complete;
  source.code = 0;
  return round_trip(source, decoded) && decoded.code == 0;
}

bool test_handshake_and_negotiation() {
  const std::vector<std::string> requested{"control", "logs", "unknown", "control"};
  const std::vector<std::string> supported{"logs", "control", "diagnostics"};
  gneiss::ipc_frame hello;
  if (gneiss::make_ipc_hello("secret", requested, hello) != gneiss::result::success) {
    return false;
  }
  hello.protocol_minor = 7U;
  gneiss::ipc_frame acknowledgment;
  std::vector<std::string> server_negotiated;
  if (gneiss::accept_ipc_hello(hello, "secret", supported, acknowledgment, server_negotiated) !=
          gneiss::result::success ||
      server_negotiated != std::vector<std::string>({"control", "logs"}) ||
      acknowledgment.protocol_minor != gneiss::ipc_protocol_minor) {
    return false;
  }
  std::vector<std::string> client_negotiated;
  if (gneiss::accept_ipc_hello_ack(acknowledgment, requested, client_negotiated) !=
          gneiss::result::success ||
      client_negotiated != server_negotiated) {
    return false;
  }

  gneiss::ipc_frame unused;
  std::vector<std::string> unchanged{"unchanged"};
  if (gneiss::accept_ipc_hello(hello, "wrong", supported, unused, unchanged) !=
          gneiss::result::invalid_argument ||
      unchanged != std::vector<std::string>{"unchanged"}) {
    return false;
  }
  hello.protocol_major += 1U;
  return gneiss::accept_ipc_hello(hello, "secret", supported, unused, unchanged) ==
         gneiss::result::unsupported;
}

bool test_invalid_and_unknown_messages() {
  gneiss::ipc_message decoded;
  gneiss::ipc_frame frame;
  frame.protocol_major = gneiss::ipc_protocol_major;
  frame.protocol_minor = gneiss::ipc_protocol_minor;
  frame.message_type = 999U;
  frame.payload = {'{', '}'};
  if (gneiss::decode_ipc_message(frame, decoded) != gneiss::result::unsupported) {
    return false;
  }
  frame.message_type = static_cast<std::uint16_t>(gneiss::ipc_message_type::ping);
  frame.payload = {'{', '"', 'n', 'o', 'n', 'c', 'e', '"', ':', '"', 'x', '"', '}'};
  if (gneiss::decode_ipc_message(frame, decoded) != gneiss::result::invalid_argument) {
    return false;
  }
  frame.payload = {'n', 'u', 'l', 'l'};
  if (gneiss::decode_ipc_message(frame, decoded) != gneiss::result::invalid_argument) {
    return false;
  }

  gneiss::ipc_message oversized;
  oversized.type = gneiss::ipc_message_type::log_event;
  oversized.text.assign(17U * 1024U, 'x');
  return gneiss::encode_ipc_message(oversized, frame) == gneiss::result::invalid_argument;
}

bool test_handshake_and_heartbeat_timeouts() {
  using namespace std::chrono_literals;
  const auto start = gneiss::ipc_timeout_tracker::clock::time_point{};
  gneiss::ipc_timeout_tracker handshake(3s);
  handshake.reset(start);
  if (handshake.expired(start + 2999ms) || !handshake.expired(start + 3s)) {
    return false;
  }

  gneiss::ipc_timeout_tracker heartbeat(5s);
  heartbeat.reset(start);
  heartbeat.reset(start + 4s);
  if (heartbeat.expired(start + 8s) || !heartbeat.expired(start + 9s)) {
    return false;
  }
  gneiss::ipc_timeout_tracker invalid(0s);
  invalid.reset(start);
  return invalid.expired(start);
}

bool wait_for_frame(gneiss::ipc_transport& transport, gneiss::ipc_frame& output) {
  using namespace std::chrono_literals;
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline) {
    std::vector<gneiss::ipc_transport_event> events;
    (void)transport.poll_events(events);
    for (auto& event : events) {
      if (event.type == gneiss::ipc_transport_event_type::frame_received) {
        output = std::move(event.frame);
        return true;
      }
    }
    std::this_thread::sleep_for(1ms);
  }
  return false;
}

bool wait_until_connected(gneiss::ipc_transport& transport) {
  using namespace std::chrono_literals;
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline) {
    if (transport.state() == gneiss::ipc_transport_state::connected) {
      return true;
    }
    std::this_thread::sleep_for(1ms);
  }
  return false;
}

bool test_handshake_over_transport() {
  gneiss::ipc_transport server;
  gneiss::ipc_transport client;
  const std::vector<std::string> requested{"control", "logs"};
  const std::vector<std::string> supported{"control"};
  if (server.start_server() != gneiss::result::success ||
      client.start_client(server.endpoint()) != gneiss::result::success ||
      !wait_until_connected(server) || !wait_until_connected(client)) {
    return false;
  }

  gneiss::ipc_frame hello;
  gneiss::ipc_frame received;
  gneiss::ipc_frame acknowledgment;
  std::vector<std::string> negotiated;
  const auto completed =
      gneiss::make_ipc_hello("transport-secret", requested, hello) == gneiss::result::success &&
      client.send(hello) == gneiss::result::success && wait_for_frame(server, received) &&
      gneiss::accept_ipc_hello(received, "transport-secret", supported, acknowledgment,
                               negotiated) == gneiss::result::success &&
      server.send(acknowledgment) == gneiss::result::success && wait_for_frame(client, received) &&
      gneiss::accept_ipc_hello_ack(received, requested, negotiated) == gneiss::result::success &&
      negotiated == std::vector<std::string>{"control"};
  return client.stop() == gneiss::result::success && server.stop() == gneiss::result::success &&
         completed;
}

} // namespace

int main() {
  return test_all_message_types() && test_handshake_and_negotiation() &&
                 test_invalid_and_unknown_messages() && test_handshake_and_heartbeat_timeouts() &&
                 test_handshake_over_transport()
             ? 0
             : 1;
}
