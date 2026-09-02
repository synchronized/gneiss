// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ipc_transport.h"

#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;

gneiss::ipc_frame make_frame(std::uint16_t type, std::vector<std::uint8_t> payload) {
  gneiss::ipc_frame frame;
  frame.protocol_major = 1U;
  frame.protocol_minor = 0U;
  frame.message_type = type;
  frame.flags = 2U;
  frame.payload = std::move(payload);
  return frame;
}

bool wait_for_event(gneiss::ipc_transport& transport, gneiss::ipc_transport_event_type type,
                    gneiss::ipc_transport_event* output = nullptr) {
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline) {
    std::vector<gneiss::ipc_transport_event> events;
    (void)transport.poll_events(events);
    for (auto& event : events) {
      if (event.type == type) {
        if (output != nullptr) {
          *output = std::move(event);
        }
        return true;
      }
    }
    std::this_thread::sleep_for(1ms);
  }
  return false;
}

bool same_frame(const gneiss::ipc_frame& left, const gneiss::ipc_frame& right) {
  return left.protocol_major == right.protocol_major &&
         left.protocol_minor == right.protocol_minor && left.message_type == right.message_type &&
         left.flags == right.flags && left.payload == right.payload;
}

bool connect(gneiss::ipc_transport& server, gneiss::ipc_transport& client) {
  return client.start_client(server.endpoint()) == gneiss::result::success &&
         wait_for_event(server, gneiss::ipc_transport_event_type::connected) &&
         wait_for_event(client, gneiss::ipc_transport_event_type::connected) &&
         server.state() == gneiss::ipc_transport_state::connected &&
         client.state() == gneiss::ipc_transport_state::connected;
}

bool test_lifecycle_and_bidirectional_frames() {
  gneiss::ipc_transport server;
  gneiss::ipc_transport client;
  if (server.send(make_frame(1U, {})) != gneiss::result::not_ready ||
      server.start_server() != gneiss::result::success ||
      server.start_server() != gneiss::result::invalid_state ||
      server.endpoint().address != "127.0.0.1" || server.endpoint().port == 0U ||
      !wait_for_event(server, gneiss::ipc_transport_event_type::listening) ||
      !connect(server, client)) {
    return false;
  }

  const auto first = make_frame(7U, {1U, 2U, 3U});
  gneiss::ipc_transport_event received;
  if (client.send(first) != gneiss::result::success ||
      !wait_for_event(server, gneiss::ipc_transport_event_type::frame_received, &received) ||
      !same_frame(received.frame, first)) {
    return false;
  }

  const auto second = make_frame(8U, {4U, 5U});
  if (server.send(second) != gneiss::result::success ||
      !wait_for_event(client, gneiss::ipc_transport_event_type::frame_received, &received) ||
      !same_frame(received.frame, second)) {
    return false;
  }

  if (client.stop() != gneiss::result::success ||
      !wait_for_event(server, gneiss::ipc_transport_event_type::disconnected) ||
      server.state() != gneiss::ipc_transport_state::listening) {
    return false;
  }

  gneiss::ipc_transport replacement;
  if (!connect(server, replacement) || replacement.stop() != gneiss::result::success ||
      server.stop() != gneiss::result::success) {
    return false;
  }

  if (server.start_server() != gneiss::result::success || !connect(server, client) ||
      client.stop() != gneiss::result::success || server.stop() != gneiss::result::success) {
    return false;
  }
  return server.state() == gneiss::ipc_transport_state::stopped;
}

bool test_invalid_arguments_and_failed_connection() {
  gneiss::ipc_transport invalid(0U, 1U);
  gneiss::ipc_transport no_writes(1U, 0U);
  if (invalid.start_server() != gneiss::result::invalid_argument ||
      no_writes.start_server() != gneiss::result::invalid_argument) {
    return false;
  }
  gneiss::ipc_transport client;
  if (client.start_client({"localhost", 1U}) != gneiss::result::invalid_argument ||
      client.start_client({"127.0.0.1", 0U}) != gneiss::result::invalid_argument) {
    return false;
  }

  gneiss::ipc_transport temporary_server;
  if (temporary_server.start_server() != gneiss::result::success) {
    return false;
  }
  const auto unused_endpoint = temporary_server.endpoint();
  if (temporary_server.stop() != gneiss::result::success ||
      client.start_client(unused_endpoint) != gneiss::result::success ||
      !wait_for_event(client, gneiss::ipc_transport_event_type::error) ||
      client.state() != gneiss::ipc_transport_state::failed) {
    return false;
  }
  return client.stop() == gneiss::result::success;
}

bool wait_for_state(gneiss::ipc_transport& transport, gneiss::ipc_transport_state state) {
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline) {
    if (transport.state() == state) {
      return true;
    }
    std::this_thread::sleep_for(1ms);
  }
  return false;
}

bool test_bounded_event_queue() {
  gneiss::ipc_transport server(1U, 4U);
  gneiss::ipc_transport client;
  if (server.start_server() != gneiss::result::success ||
      client.start_client(server.endpoint()) != gneiss::result::success ||
      !wait_for_event(client, gneiss::ipc_transport_event_type::connected) ||
      !wait_for_state(server, gneiss::ipc_transport_state::connected) ||
      !wait_for_event(server, gneiss::ipc_transport_event_type::connected) ||
      server.dropped_event_count() == 0U) {
    return false;
  }
  return client.stop() == gneiss::result::success && server.stop() == gneiss::result::success;
}

} // namespace

int main() {
  if (!test_lifecycle_and_bidirectional_frames()) {
    return 1;
  }
  if (!test_invalid_arguments_and_failed_connection()) {
    return 2;
  }
  return test_bounded_event_queue() ? 0 : 3;
}
