// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime/ipc/editor_ipc_session.hpp"
#include <gneiss/engine/core/base/error.hpp>
#include <gneiss/protocol/domains.hpp>

#include "runtime/ipc/editor_ipc_router.hpp"
#include "runtime/ipc/outbound/editor_ipc_outbound.hpp"
#include <gneiss/protocol/ipc_protocol_domains.hpp>
#include <gneiss/protocol/ipc_session_protocol.hpp>

#include <algorithm>
#include <array>
#include <new>
#include <random>
#include <utility>

namespace gneiss::host {

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
  gneiss::ipc::transport server{256U, 64U, gneiss::protocol::is_discardable};
  editor_ipc_router router;
  std::vector<gneiss::protocol::domain_capability> negotiated_domains;
  std::string token;
  bool is_authenticated = false;
  bool supports_property_editing = false;
  bool supports_asset_reload = false;
  ipc_timeout_tracker heartbeat{std::chrono::seconds(10)};
  ipc_timeout_tracker handshake{std::chrono::seconds(5)};
  std::chrono::steady_clock::time_point next_ping;
  std::uint64_t next_ping_nonce = 1U;
  std::uint32_t next_request_id = 2U;

  gneiss::error dispatch(const gneiss::ipc::envelope& envelope, runtime_ipc_event& output) {
    return router.dispatch(envelope, is_authenticated, negotiated_domains, output);
  }

  gneiss::error accept_hello(const gneiss::ipc::envelope& envelope,
                             std::chrono::steady_clock::time_point now) {
    runtime_ipc_event decoded;
    auto operation = dispatch(envelope, decoded);
    ipc_session_hello acknowledgment_message;
    const auto* hello = std::get_if<runtime_hello_event>(&decoded);
    if (operation == gneiss::error::success && hello != nullptr) {
      operation = negotiate_ipc_session_hello(hello->value, token, ipc_v2_domain_capabilities(),
                                              acknowledgment_message);
    } else if (operation == gneiss::error::success) {
      operation = gneiss::error::invalid_argument;
    }
    if (operation != error::success)
      return operation;
    auto acknowledgment =
        protocol::encode(session_accepted, acknowledgment_message, envelope.request_id);
    if (!acknowledgment)
      return acknowledgment.error().error;
    operation = server.send(*acknowledgment);
    if (operation != error::success)
      return operation;
    is_authenticated = true;
    negotiated_domains = std::move(acknowledgment_message.domains);
    supports_property_editing =
        std::ranges::find(negotiated_domains, gneiss::protocol::domain::property,
                          &gneiss::protocol::domain_capability::domain) != negotiated_domains.end();
    supports_asset_reload =
        std::ranges::find(negotiated_domains, gneiss::protocol::domain::asset,
                          &gneiss::protocol::domain_capability::domain) != negotiated_domains.end();
    heartbeat.reset(now);
    next_ping = now;
    return gneiss::error::success;
  }

  gneiss::error send(gneiss::ipc::envelope envelope) { return server.send(envelope); }
};

editor_ipc_session::editor_ipc_session() : implementation_(std::make_unique<implementation>()) {}

editor_ipc_session::~editor_ipc_session() {
  if (implementation_ && implementation_->server.state() != gneiss::ipc::transport_state::stopped) {
    (void)implementation_->server.stop();
  }
}

gneiss::error editor_ipc_session::start() {
  if (!implementation_ || !implementation_->router.is_ready() ||
      implementation_->server.state() != gneiss::ipc::transport_state::stopped) {
    return gneiss::error::invalid_state;
  }
  {
    implementation_->negotiated_domains.clear();
    implementation_->token = make_session_token();
    implementation_->is_authenticated = false;
    implementation_->supports_property_editing = false;
    implementation_->supports_asset_reload = false;
    implementation_->next_request_id = 2U;
    implementation_->next_ping_nonce = 1U;
    const auto operation = implementation_->server.start_server();
    if (operation == gneiss::error::success) {
      implementation_->handshake.reset(std::chrono::steady_clock::now());
    }
    return operation;
  }
}

gneiss::error editor_ipc_session::stop() {
  if (!implementation_) {
    return gneiss::error::not_ready;
  }
  auto operation = gneiss::error::success;
  if (implementation_->server.state() != gneiss::ipc::transport_state::stopped) {
    operation = implementation_->server.stop();
  }
  implementation_->is_authenticated = false;
  implementation_->supports_property_editing = false;
  implementation_->supports_asset_reload = false;
  implementation_->negotiated_domains.clear();
  implementation_->token.clear();
  implementation_->next_ping = {};
  return operation;
}

gneiss::error editor_ipc_session::update(bool is_peer_running,
                                         std::vector<runtime_ipc_event>& output) {
  if (!implementation_) {
    return gneiss::error::invalid_state;
  }
  const auto now = std::chrono::steady_clock::now();
  std::vector<gneiss::ipc::transport_event> events;
  (void)implementation_->server.drain_events(events);
  {
    auto transport_failure = gneiss::error::success;
    for (auto& event : events) {
      if (event.type == gneiss::ipc::transport_event_type::error ||
          event.type == gneiss::ipc::transport_event_type::disconnected) {
        if (is_peer_running) {
          transport_failure =
              event.operation == gneiss::error::success ? gneiss::error::io : event.operation;
        }
        continue;
      }
      if (event.type != gneiss::ipc::transport_event_type::envelope_received) {
        continue;
      }
      if (!implementation_->is_authenticated) {
        const auto accepted = implementation_->accept_hello(event.envelope, now);
        if (accepted != gneiss::error::success) {
          return accepted;
        }
        continue;
      }
      runtime_ipc_event decoded;
      const auto operation = implementation_->dispatch(event.envelope, decoded);
      if (operation != gneiss::error::success) {
        return operation;
      }
      implementation_->heartbeat.reset(now);
      output.push_back(std::move(decoded));
    }
    const auto received_shutdown = std::ranges::any_of(output, [](const runtime_ipc_event& event) {
      return std::holds_alternative<runtime_shutdown_event>(event);
    });
    if (transport_failure != gneiss::error::success && !received_shutdown) {
      return transport_failure;
    }
    // 终态帧必须交给上层；此时继续发送心跳可能因对端已关闭而掩盖已收到的终态。
    if (received_shutdown || !is_peer_running) {
      return gneiss::error::success;
    }
    if (!implementation_->is_authenticated) {
      return is_peer_running && implementation_->handshake.expired(now) ? gneiss::error::not_ready
                                                                        : gneiss::error::success;
    }
    if (now >= implementation_->next_ping) {
      auto ping = protocol::encode(
          session_ping, ipc_session_heartbeat{.nonce = implementation_->next_ping_nonce++},
          implementation_->next_request_id++);
      const auto operation = ping ? implementation_->send(std::move(*ping)) : ping.error().error;
      if (operation != gneiss::error::success) {
        return operation;
      }
      implementation_->next_ping = now + std::chrono::seconds(1);
    }
    return implementation_->heartbeat.expired(now) ? gneiss::error::not_ready
                                                   : gneiss::error::success;
  }
}

gneiss::error editor_ipc_session::request_stop() {
  if (!is_authenticated()) {
    return gneiss::error::not_ready;
  }
  gneiss::ipc::envelope command;
  auto operation = make_stop_command(implementation_->next_request_id++, command);
  return operation == gneiss::error::success ? implementation_->send(std::move(command))
                                             : operation;
}

gneiss::error editor_ipc_session::request_pause() {
  if (!is_authenticated()) {
    return gneiss::error::not_ready;
  }
  gneiss::ipc::envelope command;
  auto operation = make_pause_command(implementation_->next_request_id++, command);
  return operation == gneiss::error::success ? implementation_->send(std::move(command))
                                             : operation;
}

gneiss::error editor_ipc_session::request_resume() {
  if (!is_authenticated()) {
    return gneiss::error::not_ready;
  }
  gneiss::ipc::envelope command;
  auto operation = make_resume_command(implementation_->next_request_id++, command);
  return operation == gneiss::error::success ? implementation_->send(std::move(command))
                                             : operation;
}

gneiss::error editor_ipc_session::request_inspection_resync() {
  if (!is_authenticated()) {
    return gneiss::error::not_ready;
  }
  gneiss::ipc::envelope command;
  auto operation = make_inspection_resync_command(implementation_->next_request_id++, command);
  return operation == gneiss::error::success ? implementation_->send(std::move(command))
                                             : operation;
}

gneiss::error editor_ipc_session::send_property_write(const ipc_property_write& value) {
  if (!is_authenticated() || !supports_property_editing()) {
    return gneiss::error::not_ready;
  }
  if (value.command_id > UINT32_MAX)
    return error::invalid_argument;
  auto command =
      protocol::encode(property_write, value, static_cast<std::uint32_t>(value.command_id));
  return command ? implementation_->send(std::move(*command)) : command.error().error;
}

gneiss::error editor_ipc_session::send_asset_cancel(std::uint64_t session, std::uint64_t revision) {
  if (!is_authenticated() || !supports_asset_reload())
    return gneiss::error::not_ready;
  auto encoded = protocol::encode(asset_cancel, ipc_asset_cancel_request{session, revision});
  return encoded ? implementation_->send(std::move(*encoded)) : encoded.error().error;
}

gneiss::error editor_ipc_session::send_asset_reload(const ipc_asset_reload_request& command,
                                                    ipc_asset_operation operation) {
  if (!is_authenticated() || !supports_asset_reload()) {
    return gneiss::error::not_ready;
  }
  if (operation != ipc_asset_operation::reload && operation != ipc_asset_operation::resync)
    return error::invalid_argument;
  auto encoded =
      protocol::encode(operation == ipc_asset_operation::reload ? asset_reload : asset_resync,
                       command, implementation_->next_request_id++);
  return encoded ? implementation_->send(std::move(*encoded)) : encoded.error().error;
}

bool editor_ipc_session::is_authenticated() const noexcept {
  return implementation_ && implementation_->is_authenticated;
}

bool editor_ipc_session::supports_scene_loading() const noexcept {
  return implementation_ &&
         std::ranges::find(implementation_->negotiated_domains, gneiss::protocol::domain::scene,
                           &gneiss::protocol::domain_capability::domain) !=
             implementation_->negotiated_domains.end();
}
gneiss::error editor_ipc_session::send_scene_load(const ipc_scene_request& value) {
  if (!is_authenticated() || !supports_scene_loading())
    return gneiss::error::not_ready;
  auto encoded = protocol::encode(scene_load, value, implementation_->next_request_id++);
  return encoded ? implementation_->send(std::move(*encoded)) : encoded.error().error;
}

gneiss::error editor_ipc_session::send_scene_cancel(const ipc_scene_request& value) {
  if (!is_authenticated() || !supports_scene_loading())
    return gneiss::error::not_ready;
  auto encoded = protocol::encode(scene_cancel, value);
  return encoded ? implementation_->send(std::move(*encoded)) : encoded.error().error;
}

bool editor_ipc_session::supports_property_editing() const noexcept {
  return implementation_ && implementation_->supports_property_editing;
}

bool editor_ipc_session::supports_asset_reload() const noexcept {
  return implementation_ && implementation_->supports_asset_reload;
}

gneiss::ipc::endpoint editor_ipc_session::endpoint() const {
  return implementation_ ? implementation_->server.endpoint() : gneiss::ipc::endpoint{};
}

const std::string& editor_ipc_session::token() const noexcept {
  static const std::string empty;
  return implementation_ ? implementation_->token : empty;
}

} // namespace gneiss::host
