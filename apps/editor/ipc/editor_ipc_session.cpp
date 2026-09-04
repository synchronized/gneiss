// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "editor_ipc_session.h"

#include "editor_ipc_commands.h"
#include "editor_ipc_router.h"
#include "ipc_protocol_domains.h"
#include "ipc_session_protocol.h"

#include <algorithm>
#include <array>
#include <new>
#include <random>
#include <utility>

namespace gneiss::editor {

namespace {

std::string make_session_token() {
  constexpr char digits[] = "0123456789abcdef";
  std::array<unsigned char, 32U> bytes{};
  std::random_device random;
  std::string token(bytes.size() * 2U, '\0');
  for (std::size_t index = 0U; index < bytes.size(); ++index) {
    bytes[index] = static_cast<unsigned char>(random());
    token[index * 2U] = digits[bytes[index] >> 4U];
    token[index * 2U + 1U] = digits[bytes[index] & 0x0FU];
  }
  return token;
}

} // namespace

struct editor_ipc_session::implementation final {
  ipc_transport server;
  editor_ipc_router router;
  std::vector<ipc_domain_capability> negotiated_domains;
  std::string token;
  bool is_authenticated = false;
  bool supports_property_editing = false;
  ipc_timeout_tracker heartbeat{std::chrono::seconds(10)};
  ipc_timeout_tracker handshake{std::chrono::seconds(5)};
  std::chrono::steady_clock::time_point next_ping;
  std::uint64_t next_ping_nonce = 1U;
  std::uint32_t next_request_id = 2U;

  result dispatch(const ipc_envelope& envelope, runtime_ipc_event& output) noexcept {
    return router.dispatch(envelope, is_authenticated, negotiated_domains, output);
  }

  result accept_hello(const ipc_envelope& envelope,
                      std::chrono::steady_clock::time_point now) noexcept {
    runtime_ipc_event decoded;
    auto operation = dispatch(envelope, decoded);
    ipc_session_hello acknowledgment_message;
    const auto* hello = std::get_if<runtime_hello_event>(&decoded);
    if (operation == result::success && hello != nullptr) {
      operation = negotiate_ipc_session_hello(hello->value, token, ipc_v2_domain_capabilities(),
                                              acknowledgment_message);
    } else if (operation == result::success) {
      operation = result::invalid_argument;
    }
    ipc_envelope acknowledgment;
    if (operation == result::success) {
      operation = encode_ipc_session_hello(acknowledgment_message, true, envelope.request_id,
                                           acknowledgment);
    }
    if (operation == result::success) {
      operation = server.send(acknowledgment);
    }
    if (operation != result::success) {
      return operation;
    }
    is_authenticated = true;
    negotiated_domains = std::move(acknowledgment_message.domains);
    supports_property_editing =
        std::ranges::find(negotiated_domains, ipc_domain::property,
                          &ipc_domain_capability::domain) != negotiated_domains.end();
    heartbeat.reset(now);
    next_ping = now;
    return result::success;
  }

  result send(ipc_envelope envelope) noexcept { return server.send(envelope); }
};

editor_ipc_session::editor_ipc_session() : implementation_(std::make_unique<implementation>()) {}

editor_ipc_session::~editor_ipc_session() {
  if (implementation_ && implementation_->server.state() != ipc_transport_state::stopped) {
    (void)implementation_->server.stop();
  }
}

result editor_ipc_session::start() noexcept {
  if (!implementation_ || !implementation_->router.is_ready() ||
      implementation_->server.state() != ipc_transport_state::stopped) {
    return result::invalid_state;
  }
  try {
    implementation_->negotiated_domains.clear();
    implementation_->token = make_session_token();
    implementation_->is_authenticated = false;
    implementation_->supports_property_editing = false;
    implementation_->next_request_id = 2U;
    implementation_->next_ping_nonce = 1U;
    const auto operation = implementation_->server.start_server();
    if (operation == result::success) {
      implementation_->handshake.reset(std::chrono::steady_clock::now());
    }
    return operation;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

result editor_ipc_session::stop() noexcept {
  if (!implementation_) {
    return result::not_ready;
  }
  auto operation = result::success;
  if (implementation_->server.state() != ipc_transport_state::stopped) {
    operation = implementation_->server.stop();
  }
  implementation_->is_authenticated = false;
  implementation_->supports_property_editing = false;
  implementation_->negotiated_domains.clear();
  implementation_->token.clear();
  implementation_->next_ping = {};
  return operation;
}

result editor_ipc_session::update(bool is_peer_running,
                                  std::vector<runtime_ipc_event>& output) noexcept {
  if (!implementation_) {
    return result::invalid_state;
  }
  const auto now = std::chrono::steady_clock::now();
  std::vector<ipc_transport_event> events;
  (void)implementation_->server.poll_events(events);
  try {
    auto transport_failure = result::success;
    for (auto& event : events) {
      if (event.type == ipc_transport_event_type::error ||
          event.type == ipc_transport_event_type::disconnected) {
        if (is_peer_running) {
          transport_failure = event.operation == result::success ? result::io : event.operation;
        }
        continue;
      }
      if (event.type != ipc_transport_event_type::envelope_received) {
        continue;
      }
      if (!implementation_->is_authenticated) {
        const auto accepted = implementation_->accept_hello(event.envelope, now);
        if (accepted != result::success) {
          return accepted;
        }
        continue;
      }
      runtime_ipc_event decoded;
      const auto operation = implementation_->dispatch(event.envelope, decoded);
      if (operation != result::success) {
        return operation;
      }
      implementation_->heartbeat.reset(now);
      output.push_back(std::move(decoded));
    }
    const auto received_shutdown = std::ranges::any_of(output, [](const runtime_ipc_event& event) {
      return std::holds_alternative<runtime_shutdown_event>(event);
    });
    if (transport_failure != result::success && !received_shutdown) {
      return transport_failure;
    }
    if (!implementation_->is_authenticated) {
      return is_peer_running && implementation_->handshake.expired(now) ? result::not_ready
                                                                        : result::success;
    }
    if (now >= implementation_->next_ping) {
      ipc_envelope ping;
      auto operation = make_heartbeat_command(implementation_->next_ping_nonce++,
                                              implementation_->next_request_id++, ping);
      if (operation == result::success) {
        operation = implementation_->send(std::move(ping));
      }
      if (operation != result::success) {
        return operation;
      }
      implementation_->next_ping = now + std::chrono::seconds(1);
    }
    return implementation_->heartbeat.expired(now) ? result::not_ready : result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

result editor_ipc_session::request_stop() noexcept {
  if (!is_authenticated()) {
    return result::not_ready;
  }
  ipc_envelope command;
  auto operation = make_stop_command(implementation_->next_request_id++, command);
  return operation == result::success ? implementation_->send(std::move(command)) : operation;
}

result editor_ipc_session::request_pause() noexcept {
  if (!is_authenticated()) {
    return result::not_ready;
  }
  ipc_envelope command;
  auto operation = make_pause_command(implementation_->next_request_id++, command);
  return operation == result::success ? implementation_->send(std::move(command)) : operation;
}

result editor_ipc_session::request_resume() noexcept {
  if (!is_authenticated()) {
    return result::not_ready;
  }
  ipc_envelope command;
  auto operation = make_resume_command(implementation_->next_request_id++, command);
  return operation == result::success ? implementation_->send(std::move(command)) : operation;
}

result editor_ipc_session::request_inspection_resync() noexcept {
  if (!is_authenticated()) {
    return result::not_ready;
  }
  ipc_envelope command;
  auto operation = make_inspection_resync_command(implementation_->next_request_id++, command);
  return operation == result::success ? implementation_->send(std::move(command)) : operation;
}

result editor_ipc_session::send_property_write(const ipc_property_write& value) noexcept {
  if (!is_authenticated() || !supports_property_editing()) {
    return result::not_ready;
  }
  ipc_envelope command;
  auto operation = make_property_write_command(value, command);
  return operation == result::success ? implementation_->send(std::move(command)) : operation;
}

bool editor_ipc_session::is_authenticated() const noexcept {
  return implementation_ && implementation_->is_authenticated;
}

bool editor_ipc_session::supports_property_editing() const noexcept {
  return implementation_ && implementation_->supports_property_editing;
}

ipc_endpoint editor_ipc_session::endpoint() const noexcept {
  return implementation_ ? implementation_->server.endpoint() : ipc_endpoint{};
}

const std::string& editor_ipc_session::token() const noexcept {
  static const std::string empty;
  return implementation_ ? implementation_->token : empty;
}

} // namespace gneiss::editor
