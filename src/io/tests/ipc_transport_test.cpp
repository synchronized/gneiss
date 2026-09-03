// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ipc_transport.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;

#define GNEISS_TEST_CHECK(expression)                                                              \
  do {                                                                                             \
    if (!(expression)) {                                                                           \
      std::fprintf(stderr, "IPC Transport 测试失败：行=%d，表达式=%s\n", __LINE__, #expression);   \
      return false;                                                                                \
    }                                                                                              \
  } while (false)

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

bool same_envelope(const gneiss::ipc_envelope& left, const gneiss::ipc_envelope& right) {
  return left.protocol_major == right.protocol_major &&
         left.protocol_minor == right.protocol_minor && left.domain == right.domain &&
         left.operation == right.operation && left.kind == right.kind &&
         left.request_id == right.request_id && left.payload == right.payload;
}

bool wait_for_state(gneiss::ipc_transport& transport, gneiss::ipc_transport_state state);

bool connect(gneiss::ipc_transport& server, gneiss::ipc_transport& client) {
  GNEISS_TEST_CHECK(client.start_client(server.endpoint()) == gneiss::result::success);
  GNEISS_TEST_CHECK(wait_for_event(server, gneiss::ipc_transport_event_type::connected));
  GNEISS_TEST_CHECK(wait_for_event(client, gneiss::ipc_transport_event_type::connected));
  GNEISS_TEST_CHECK(server.state() == gneiss::ipc_transport_state::connected);
  GNEISS_TEST_CHECK(client.state() == gneiss::ipc_transport_state::connected);
  return true;
}

bool test_lifecycle_and_bidirectional_frames() {
  gneiss::ipc_transport server;
  gneiss::ipc_transport client;
  GNEISS_TEST_CHECK(server.send(make_frame(1U, {})) == gneiss::result::not_ready);
  GNEISS_TEST_CHECK(server.start_server() == gneiss::result::success);
  GNEISS_TEST_CHECK(server.start_server() == gneiss::result::invalid_state);
  GNEISS_TEST_CHECK(server.endpoint().address == "127.0.0.1");
  GNEISS_TEST_CHECK(server.endpoint().port != 0U);
  GNEISS_TEST_CHECK(wait_for_event(server, gneiss::ipc_transport_event_type::listening));
  GNEISS_TEST_CHECK(connect(server, client));

  const auto first = make_frame(7U, {1U, 2U, 3U});
  gneiss::ipc_transport_event received;
  GNEISS_TEST_CHECK(client.send(first) == gneiss::result::success);
  GNEISS_TEST_CHECK(
      wait_for_event(server, gneiss::ipc_transport_event_type::frame_received, &received));
  GNEISS_TEST_CHECK(same_frame(received.frame, first));

  const auto second = make_frame(8U, {4U, 5U});
  GNEISS_TEST_CHECK(server.send(second) == gneiss::result::success);
  GNEISS_TEST_CHECK(
      wait_for_event(client, gneiss::ipc_transport_event_type::frame_received, &received));
  GNEISS_TEST_CHECK(same_frame(received.frame, second));

  GNEISS_TEST_CHECK(client.stop() == gneiss::result::success);
  GNEISS_TEST_CHECK(wait_for_event(server, gneiss::ipc_transport_event_type::disconnected));
  GNEISS_TEST_CHECK(wait_for_state(server, gneiss::ipc_transport_state::listening));

  gneiss::ipc_transport replacement;
  GNEISS_TEST_CHECK(connect(server, replacement));
  GNEISS_TEST_CHECK(replacement.stop() == gneiss::result::success);
  GNEISS_TEST_CHECK(server.stop() == gneiss::result::success);

  GNEISS_TEST_CHECK(server.start_server() == gneiss::result::success);
  GNEISS_TEST_CHECK(connect(server, client));
  GNEISS_TEST_CHECK(client.stop() == gneiss::result::success);
  GNEISS_TEST_CHECK(server.stop() == gneiss::result::success);
  GNEISS_TEST_CHECK(server.state() == gneiss::ipc_transport_state::stopped);
  return true;
}

bool test_invalid_arguments_and_failed_connection() {
  gneiss::ipc_transport invalid(0U, 1U);
  gneiss::ipc_transport no_writes(1U, 0U);
  GNEISS_TEST_CHECK(invalid.start_server() == gneiss::result::invalid_argument);
  GNEISS_TEST_CHECK(no_writes.start_server() == gneiss::result::invalid_argument);
  gneiss::ipc_transport client;
  GNEISS_TEST_CHECK(client.start_client({"localhost", 1U}) == gneiss::result::invalid_argument);
  GNEISS_TEST_CHECK(client.start_client({"127.0.0.1", 0U}) == gneiss::result::invalid_argument);
  const auto invalid_protocol = static_cast<gneiss::ipc_transport_protocol>(0xFFU);
  GNEISS_TEST_CHECK(client.start_client({"127.0.0.1", 1U}, invalid_protocol) ==
                    gneiss::result::invalid_argument);
  GNEISS_TEST_CHECK(client.start_server(invalid_protocol) == gneiss::result::invalid_argument);

  gneiss::ipc_transport temporary_server;
  GNEISS_TEST_CHECK(temporary_server.start_server() == gneiss::result::success);
  const auto unused_endpoint = temporary_server.endpoint();
  GNEISS_TEST_CHECK(temporary_server.stop() == gneiss::result::success);
  GNEISS_TEST_CHECK(client.start_client(unused_endpoint) == gneiss::result::success);
  GNEISS_TEST_CHECK(wait_for_event(client, gneiss::ipc_transport_event_type::error));
  GNEISS_TEST_CHECK(client.state() == gneiss::ipc_transport_state::failed);
  GNEISS_TEST_CHECK(client.stop() == gneiss::result::success);
  return true;
}

bool test_v2_envelopes() {
  gneiss::ipc_transport server;
  gneiss::ipc_transport client;
  GNEISS_TEST_CHECK(server.start_server(gneiss::ipc_transport_protocol::envelope_v2) ==
                    gneiss::result::success);
  GNEISS_TEST_CHECK(
      client.start_client(server.endpoint(), gneiss::ipc_transport_protocol::envelope_v2) ==
      gneiss::result::success);
  GNEISS_TEST_CHECK(wait_for_event(server, gneiss::ipc_transport_event_type::connected));
  GNEISS_TEST_CHECK(wait_for_event(client, gneiss::ipc_transport_event_type::connected));
  const gneiss::ipc_envelope sent{.domain = gneiss::ipc_domain::control,
                                  .operation = 3U,
                                  .kind = gneiss::ipc_message_kind::request,
                                  .request_id = 7U,
                                  .payload = {1U, 2U}};
  GNEISS_TEST_CHECK(client.send(sent) == gneiss::result::success);
  GNEISS_TEST_CHECK(client.send(make_frame(1U, {})) == gneiss::result::not_ready);
  gneiss::ipc_transport_event received;
  GNEISS_TEST_CHECK(
      wait_for_event(server, gneiss::ipc_transport_event_type::envelope_received, &received));
  GNEISS_TEST_CHECK(same_envelope(received.envelope, sent));
  GNEISS_TEST_CHECK(client.stop() == gneiss::result::success);
  GNEISS_TEST_CHECK(server.stop() == gneiss::result::success);
  return true;
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
  GNEISS_TEST_CHECK(server.start_server() == gneiss::result::success);
  GNEISS_TEST_CHECK(client.start_client(server.endpoint()) == gneiss::result::success);
  GNEISS_TEST_CHECK(wait_for_event(client, gneiss::ipc_transport_event_type::connected));
  GNEISS_TEST_CHECK(wait_for_state(server, gneiss::ipc_transport_state::connected));
  GNEISS_TEST_CHECK(wait_for_event(server, gneiss::ipc_transport_event_type::connected));
  GNEISS_TEST_CHECK(server.dropped_event_count() != 0U);
  GNEISS_TEST_CHECK(client.stop() == gneiss::result::success);
  GNEISS_TEST_CHECK(server.stop() == gneiss::result::success);
  return true;
}

} // namespace

int main() {
  if (!test_lifecycle_and_bidirectional_frames()) {
    return 1;
  }
  if (!test_invalid_arguments_and_failed_connection()) {
    return 2;
  }
  if (!test_v2_envelopes()) {
    return 3;
  }
  return test_bounded_event_queue() ? 0 : 4;
}
