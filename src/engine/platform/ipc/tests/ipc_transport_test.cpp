// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ipc_transport.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <source_location>
#include <stdexcept>
#include <thread>
#include <uv.h>
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

gneiss::ipc_envelope make_envelope(std::uint16_t operation, std::vector<std::uint8_t> payload) {
  return {.domain = gneiss::ipc_domain::control,
          .operation = operation,
          .kind = gneiss::ipc_message_kind::request,
          .request_id = static_cast<std::uint32_t>(operation),
          .payload = std::move(payload)};
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

bool test_lifecycle_and_bidirectional_envelopes() {
  gneiss::ipc_transport server;
  gneiss::ipc_transport client;
  GNEISS_TEST_CHECK(server.send(make_envelope(1U, {})) == gneiss::result::not_ready);
  GNEISS_TEST_CHECK(server.start_server() == gneiss::result::success);
  GNEISS_TEST_CHECK(server.start_server() == gneiss::result::invalid_state);
  GNEISS_TEST_CHECK(server.endpoint().address == "127.0.0.1");
  GNEISS_TEST_CHECK(server.endpoint().port != 0U);
  GNEISS_TEST_CHECK(wait_for_event(server, gneiss::ipc_transport_event_type::listening));
  GNEISS_TEST_CHECK(connect(server, client));

  const auto first = make_envelope(7U, {1U, 2U, 3U});
  gneiss::ipc_transport_event received;
  GNEISS_TEST_CHECK(client.send(first) == gneiss::result::success);
  GNEISS_TEST_CHECK(
      wait_for_event(server, gneiss::ipc_transport_event_type::envelope_received, &received));
  GNEISS_TEST_CHECK(same_envelope(received.envelope, first));

  const auto second = make_envelope(8U, {4U, 5U});
  GNEISS_TEST_CHECK(server.send(second) == gneiss::result::success);
  GNEISS_TEST_CHECK(
      wait_for_event(client, gneiss::ipc_transport_event_type::envelope_received, &received));
  GNEISS_TEST_CHECK(same_envelope(received.envelope, second));

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

bool test_log_backpressure_preserves_control() {
  gneiss::ipc_transport server(2U, 64U);
  gneiss::ipc_transport client;
  GNEISS_TEST_CHECK(server.start_server() == gneiss::result::success);
  GNEISS_TEST_CHECK(wait_for_event(server, gneiss::ipc_transport_event_type::listening));
  GNEISS_TEST_CHECK(connect(server, client));
  const gneiss::ipc_envelope log{.domain = gneiss::ipc_domain::log, .operation = 1U, .payload = {}};
  const auto wait_dropped = [&](std::size_t count) {
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (server.dropped_event_count() < count && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(1ms);
    }
    return server.dropped_event_count() == count;
  };
  for (unsigned i = 0U; i < 3U; ++i) {
    GNEISS_TEST_CHECK(client.send(log) == gneiss::result::success);
  }
  GNEISS_TEST_CHECK(wait_dropped(1U));
  const auto control = make_envelope(7U, {1U});
  GNEISS_TEST_CHECK(client.send(control) == gneiss::result::success);
  GNEISS_TEST_CHECK(client.send(log) == gneiss::result::success);
  GNEISS_TEST_CHECK(wait_dropped(3U));
  std::vector<gneiss::ipc_transport_event> events;
  GNEISS_TEST_CHECK(server.poll_events(events) == 2U);
  GNEISS_TEST_CHECK(events.front().envelope.domain == gneiss::ipc_domain::log);
  GNEISS_TEST_CHECK(same_envelope(events.back().envelope, control));
  GNEISS_TEST_CHECK(client.stop() == gneiss::result::success);
  GNEISS_TEST_CHECK(server.stop() == gneiss::result::success);
  return true;
}

// 不启动读操作，使服务端发送缓冲确定性积压；半关闭和复位均由测试显式发起。
class controlled_peer final {
public:
  controlled_peer() = default;
  controlled_peer(const controlled_peer&) = delete;
  controlled_peer& operator=(const controlled_peer&) = delete;
  ~controlled_peer() {
    if (stream_ready_ && uv_is_closing(reinterpret_cast<uv_handle_t*>(&stream_)) == 0) {
      uv_close(reinterpret_cast<uv_handle_t*>(&stream_), nullptr);
    }
    if (loop_ready_) {
      (void)uv_run(&loop_, UV_RUN_DEFAULT);
      (void)uv_loop_close(&loop_);
    }
  }
  bool connect(const gneiss::ipc_endpoint& endpoint) {
    loop_ready_ = uv_loop_init(&loop_) == 0;
    if (!loop_ready_) {
      return false;
    }
    stream_ready_ = uv_tcp_init(&loop_, &stream_) == 0;
    if (!stream_ready_) {
      return false;
    }
    sockaddr_in address{};
    if (uv_ip4_addr(endpoint.address.c_str(), endpoint.port, &address) != 0) {
      return false;
    }
    int receive_bytes = 4096;
    // 收窄接收窗口；与服务端 pending_write_count 联合确认积压，而非依赖 sleep。
    request_.data = this;
    const auto started =
        uv_tcp_connect(&request_, &stream_, reinterpret_cast<const sockaddr*>(&address),
                       [](uv_connect_t* request, int status) {
                         auto& self = *static_cast<controlled_peer*>(request->data);
                         self.connected_ = true;
                         self.status_ = status;
                       });
    if (started != 0) {
      return false;
    }
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!connected_ && std::chrono::steady_clock::now() < deadline) {
      (void)uv_run(&loop_, UV_RUN_NOWAIT);
      std::this_thread::yield();
    }
    return connected_ && status_ == 0 &&
           uv_recv_buffer_size(reinterpret_cast<uv_handle_t*>(&stream_), &receive_bytes) == 0;
  }
  bool half_close() {
    shutdown_.data = this;
    const auto started = uv_shutdown(&shutdown_, reinterpret_cast<uv_stream_t*>(&stream_),
                                     [](uv_shutdown_t* request, int status) {
                                       auto& self = *static_cast<controlled_peer*>(request->data);
                                       self.shutdown_done_ = true;
                                       self.status_ = status;
                                     });
    if (started != 0) {
      return false;
    }
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!shutdown_done_ && std::chrono::steady_clock::now() < deadline) {
      (void)uv_run(&loop_, UV_RUN_NOWAIT);
    }
    return shutdown_done_ && status_ == 0;
  }
  bool reset() { return uv_tcp_close_reset(&stream_, nullptr) == 0; }

private:
  uv_loop_t loop_{};
  uv_tcp_t stream_{};
  uv_connect_t request_{};
  uv_shutdown_t shutdown_{};
  bool loop_ready_{};
  bool stream_ready_{};
  bool connected_{};
  bool shutdown_done_{};
  int status_{};
};

void check_disconnect(bool success, std::source_location at = std::source_location::current()) {
  if (!success) {
    throw std::runtime_error("IPC 断连测试失败，行=" + std::to_string(at.line()));
  }
}

void test_controlled_disconnect(bool reset) {
  gneiss::ipc_transport server;
  check_disconnect(server.start_server() == gneiss::result::success);
  controlled_peer peer;
  check_disconnect(peer.connect(server.endpoint()));
  check_disconnect(wait_for_event(server, gneiss::ipc_transport_event_type::connected));
  if (reset) {
    const auto large = make_envelope(11U, std::vector<std::uint8_t>(std::size_t{1024U} * 1024U));
    for (unsigned count = 0U; count < 32U; ++count) {
      check_disconnect(server.send(large) == gneiss::result::success);
    }
    check_disconnect(server.pending_write_count() > 0U);
    check_disconnect(peer.reset());
  } else {
    check_disconnect(peer.half_close());
  }
  check_disconnect(wait_for_state(server, gneiss::ipc_transport_state::listening));
  check_disconnect(server.pending_write_count() == 0U);
  check_disconnect(server.send(make_envelope(12U, {})) == gneiss::result::not_ready);
  // 断连后同一监听器可接收新会话，不遗留上一会话的写请求或帧解码状态。
  gneiss::ipc_transport replacement;
  check_disconnect(connect(server, replacement));
  check_disconnect(replacement.send(make_envelope(13U, {1U})) == gneiss::result::success);
  check_disconnect(wait_for_event(server, gneiss::ipc_transport_event_type::envelope_received));
  check_disconnect(replacement.stop() == gneiss::result::success);
  check_disconnect(server.stop() == gneiss::result::success);
}

} // namespace

int main() try {
  if (!test_lifecycle_and_bidirectional_envelopes()) {
    return 1;
  }
  if (!test_invalid_arguments_and_failed_connection()) {
    return 2;
  }
  if (!test_bounded_event_queue()) {
    return 3;
  }
  if (!test_log_backpressure_preserves_control()) {
    return 4;
  }
  test_controlled_disconnect(false);
  test_controlled_disconnect(true);
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 5;
}
