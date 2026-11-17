// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/protocol/domains.hpp>
#include "ipc/runtime_ipc_session.hpp"
#include <gneiss/engine/error.hpp>

#include "ipc/outbound/runtime_ipc_outbound.hpp"
#include "ipc/runtime_commands.hpp"

#include <algorithm>
#include <deque>
#include <optional>
#include <utility>
#include <vector>

namespace gneiss::host {

struct runtime_ipc_session::implementation final {
  explicit implementation(runtime_ipc_config value)
      : config(std::move(value)), handshake_deadline(config.handshake_timeout),
        heartbeat_deadline(config.heartbeat_timeout) {
    router_ready = register_runtime_commands(router) == gneiss::error::success;
  }

  runtime_ipc_config config;
  gneiss::ipc::transport transport{256U, 64U, gneiss::protocol::is_discardable};
  ipc_timeout_tracker handshake_deadline;
  ipc_timeout_tracker heartbeat_deadline;
  runtime_ipc_state current_state = runtime_ipc_state::stopped;
  std::vector<gneiss::protocol::domain_capability> requested_domains{
      ipc_v2_domain_capabilities().begin(), ipc_v2_domain_capabilities().end()};
  std::vector<gneiss::protocol::domain_capability> negotiated_domains;
  runtime_command_router router;
  bool router_ready = false;
  bool wants_running = false;
  std::deque<gneiss::ipc::envelope> pending_inspection_frames;

  gneiss::error dispatch(const gneiss::ipc::envelope& envelope,
                                 runtime_ipc_actions& actions) {
    if (!router_ready) {
      return gneiss::error::invalid_state;
    }
    const gneiss::protocol::dispatch_context dispatch_context{
        .remote_role = gneiss::protocol::peer_role::editor,
        .handshake_complete = true,
        .negotiated_domains = negotiated_domains};
    runtime_command_context command_context(current_state, actions, this, send_from_command);
    const auto outcome = router.dispatch(envelope, dispatch_context, command_context);
    if (!outcome.accepted()) {
      return outcome.rejection == gneiss::protocol::dispatch_rejection::handler_failed
                 ? outcome.handler_result
                 : gneiss::error::invalid_argument;
    }
    return gneiss::error::success;
  }

  static gneiss::error send_from_command(void* context,
                                                 gneiss::ipc::envelope envelope) {
    return static_cast<implementation*>(context)->send(std::move(envelope));
  }

  gneiss::error flush_inspection() {
    while (!pending_inspection_frames.empty() && transport.pending_write_count() < 48U) {
      const auto operation = transport.send(pending_inspection_frames.front());
      if (operation == gneiss::error::not_ready) {
        return gneiss::error::success;
      }
      if (operation != gneiss::error::success) {
        return operation;
      }
      pending_inspection_frames.pop_front();
    }
    return gneiss::error::success;
  }

  gneiss::error send(gneiss::ipc::envelope envelope) {
    return transport.send(envelope);
  }

  [[nodiscard]] bool negotiated(std::uint16_t domain) const noexcept {
    return std::ranges::find(negotiated_domains, domain,
                             &gneiss::protocol::domain_capability::domain) !=
           negotiated_domains.end();
  }

  gneiss::error send_state(ipc_control_state state) {
    gneiss::ipc::envelope envelope;
    const auto operation = make_state_event(state, envelope);
    return operation == gneiss::error::success ? send(std::move(envelope)) : operation;
  }

  gneiss::error enter_running() {
    gneiss::ipc::envelope ready;
    auto operation = make_ready_event(ready);
    if (operation == gneiss::error::success) {
      operation = send(std::move(ready));
    }
    if (operation == gneiss::error::success) {
      operation = send_state(ipc_control_state::running);
    }
    if (operation == gneiss::error::success) {
      current_state = runtime_ipc_state::running;
    }
    return operation;
  }

  gneiss::error fail(gneiss::error operation, runtime_ipc_actions& actions) {
    current_state = runtime_ipc_state::failed;
    actions.request_exit = true;
    actions.failure = operation;
    return operation;
  }
};

runtime_ipc_session::runtime_ipc_session(runtime_ipc_config config)
    : implementation_(std::make_unique<implementation>(std::move(config))) {}

runtime_ipc_session::~runtime_ipc_session() {
  if (implementation_) {
    (void)stop();
  }
}

gneiss::error runtime_ipc_session::start(clock::time_point now) {
  if (!implementation_ || implementation_->current_state != runtime_ipc_state::stopped ||
      implementation_->config.session_token.empty()) {
    return gneiss::error::invalid_state;
  }
  const auto operation = implementation_->transport.start_client(implementation_->config.endpoint);
  if (operation != gneiss::error::success) {
    implementation_->current_state = runtime_ipc_state::failed;
    return operation;
  }
  implementation_->handshake_deadline.reset(now);
  implementation_->current_state = runtime_ipc_state::connecting;
  return gneiss::error::success;
}

gneiss::error runtime_ipc_session::pump(clock::time_point now,
                                                runtime_ipc_actions& actions) {
  actions = {};
  if (!implementation_ || implementation_->current_state == runtime_ipc_state::stopped ||
      implementation_->current_state == runtime_ipc_state::failed) {
    return gneiss::error::invalid_state;
  }
  std::vector<gneiss::ipc::transport_event> events;
  (void)implementation_->transport.drain_events(events);
  for (auto& event : events) {
    if (event.type == gneiss::ipc::transport_event_type::connected) {
      gneiss::ipc::envelope hello;
      auto operation = make_session_hello_event(implementation_->config.session_token,
                                                implementation_->requested_domains, 1U, hello);
      if (operation == gneiss::error::success) {
        operation = implementation_->transport.send(hello);
      }
      if (operation != gneiss::error::success) {
        return implementation_->fail(operation, actions);
      }
      implementation_->current_state = runtime_ipc_state::authenticating;
      continue;
    }
    if (event.type == gneiss::ipc::transport_event_type::disconnected ||
        event.type == gneiss::ipc::transport_event_type::error) {
      const auto operation = event.operation == gneiss::error::success
                                 ? gneiss::error::io
                                 : event.operation;
      return implementation_->fail(operation, actions);
    }
    if (event.type != gneiss::ipc::transport_event_type::envelope_received) {
      continue;
    }

    if (implementation_->current_state == runtime_ipc_state::authenticating) {
      ipc_session_hello hello;
      const auto operation = decode_ipc_session_hello(event.envelope, hello);
      if (operation != gneiss::error::success ||
          event.envelope.domain != gneiss::protocol::domain::session ||
          event.envelope.operation != static_cast<std::uint16_t>(ipc_session_operation::hello) ||
          event.envelope.kind != gneiss::ipc::message_kind::response ||
          event.envelope.request_id != 1U) {
        return implementation_->fail(operation == gneiss::error::success
                                         ? gneiss::error::invalid_argument
                                         : operation,
                                     actions);
      }
      implementation_->negotiated_domains = std::move(hello.domains);
      if (std::ranges::any_of(implementation_->negotiated_domains, [&](const auto negotiated) {
            const auto requested =
                std::ranges::find(implementation_->requested_domains, negotiated.domain,
                                  &gneiss::protocol::domain_capability::domain);
            return requested == implementation_->requested_domains.end() ||
                   negotiated.version == 0U || negotiated.version > requested->version;
          })) {
        return implementation_->fail(gneiss::error::invalid_argument, actions);
      }
      if (!implementation_->negotiated(gneiss::protocol::domain::control)) {
        return implementation_->fail(gneiss::error::unsupported, actions);
      }
      implementation_->heartbeat_deadline.reset(now);
      if (implementation_->wants_running) {
        const auto running = implementation_->enter_running();
        if (running != gneiss::error::success) {
          return implementation_->fail(running, actions);
        }
      }
      continue;
    }

    const auto handled = implementation_->dispatch(event.envelope, actions);
    if (handled != gneiss::error::success) {
      return implementation_->fail(handled, actions);
    }
    implementation_->heartbeat_deadline.reset(now);
  }

  if ((implementation_->current_state == runtime_ipc_state::connecting ||
       implementation_->current_state == runtime_ipc_state::authenticating) &&
      implementation_->handshake_deadline.expired(now)) {
    return implementation_->fail(gneiss::error::not_ready, actions);
  }
  if ((implementation_->current_state == runtime_ipc_state::running ||
       implementation_->current_state == runtime_ipc_state::paused) &&
      implementation_->heartbeat_deadline.expired(now)) {
    return implementation_->fail(gneiss::error::not_ready, actions);
  }
  if (implementation_->current_state == runtime_ipc_state::running ||
      implementation_->current_state == runtime_ipc_state::paused) {
    const auto flushed = implementation_->flush_inspection();
    if (flushed != gneiss::error::success) {
      return implementation_->fail(flushed, actions);
    }
  }
  return gneiss::error::success;
}

gneiss::error runtime_ipc_session::notify_running() {
  if (!implementation_ || implementation_->current_state == runtime_ipc_state::failed ||
      implementation_->current_state == runtime_ipc_state::stopped) {
    return gneiss::error::invalid_state;
  }
  implementation_->wants_running = true;
  if (implementation_->current_state == runtime_ipc_state::authenticating ||
      implementation_->current_state == runtime_ipc_state::connecting) {
    return gneiss::error::success;
  }
  return implementation_->enter_running();
}

gneiss::error runtime_ipc_session::notify_shutdown(std::int32_t exit_code) {
  if (!implementation_ || implementation_->current_state == runtime_ipc_state::stopped ||
      implementation_->current_state == runtime_ipc_state::failed) {
    return gneiss::error::invalid_state;
  }
  implementation_->current_state = runtime_ipc_state::stopping;
  auto operation = implementation_->send_state(ipc_control_state::stopping);
  if (operation == gneiss::error::success) {
    gneiss::ipc::envelope envelope;
    operation = make_shutdown_event(exit_code, envelope);
    if (operation == gneiss::error::success) {
      operation = implementation_->send(std::move(envelope));
    }
  }
  return operation;
}

gneiss::error
runtime_ipc_session::notify_log_event(const gneiss::logging::record_view& event) {
  if (!implementation_ || (implementation_->current_state != runtime_ipc_state::running &&
                           implementation_->current_state != runtime_ipc_state::paused)) {
    return gneiss::error::not_ready;
  }
  gneiss::ipc::envelope envelope;
  const auto operation = make_log_event(event, envelope);
  return operation == gneiss::error::success ? implementation_->send(std::move(envelope))
                                                     : operation;
}

gneiss::error
runtime_ipc_session::notify_scene_snapshot(const ipc_inspection_batch& batch) {
  if (!implementation_ || !implementation_->negotiated(gneiss::protocol::domain::inspection) ||
      (implementation_->current_state != runtime_ipc_state::running &&
       implementation_->current_state != runtime_ipc_state::paused)) {
    return gneiss::error::not_ready;
  }
  if (!implementation_->pending_inspection_frames.empty()) {
    return gneiss::error::not_ready;
  }
  std::vector<gneiss::ipc::envelope> frames;
  const auto operation = make_scene_snapshot_events(batch, frames);
  if (operation != gneiss::error::success) {
    return operation;
  }
  implementation_->pending_inspection_frames.insert(
      implementation_->pending_inspection_frames.end(), std::make_move_iterator(frames.begin()),
      std::make_move_iterator(frames.end()));
  return implementation_->flush_inspection();
}

gneiss::error
runtime_ipc_session::notify_property_write_result(const ipc_property_write_result& response) {
  if (!implementation_ || !implementation_->negotiated(gneiss::protocol::domain::property) ||
      (implementation_->current_state != runtime_ipc_state::running &&
       implementation_->current_state != runtime_ipc_state::paused)) {
    return gneiss::error::not_ready;
  }
  gneiss::ipc::envelope envelope;
  const auto operation = make_property_write_result_event(
      response, static_cast<std::uint32_t>(response.command_id), envelope);
  return operation == gneiss::error::success ? implementation_->transport.send(envelope)
                                                     : operation;
}

gneiss::error
runtime_ipc_session::notify_asset_reload_result(const ipc_asset_reload_result& response,
                                                ipc_asset_operation operation,
                                                std::uint32_t request_id) {
  if (!implementation_ || !implementation_->negotiated(gneiss::protocol::domain::asset) ||
      (implementation_->current_state != runtime_ipc_state::running &&
       implementation_->current_state != runtime_ipc_state::paused)) {
    return gneiss::error::not_ready;
  }
  if (operation != ipc_asset_operation::reload && operation != ipc_asset_operation::resync &&
      operation != ipc_asset_operation::progress)
    return error::invalid_argument;
  const auto& message = operation == ipc_asset_operation::progress ? asset_progress
                        : operation == ipc_asset_operation::reload ? asset_reload_result
                                                                   : asset_resync_result;
  auto encoded = protocol::encode(message, response, request_id);
  return encoded ? implementation_->transport.send(*encoded) : encoded.error().error;
}

gneiss::error runtime_ipc_session::notify_scene_progress(const ipc_scene_progress& value,
                                                                 std::uint32_t request_id) {
  if (!implementation_ || !implementation_->negotiated(gneiss::protocol::domain::scene) ||
      (implementation_->current_state != runtime_ipc_state::running &&
       implementation_->current_state != runtime_ipc_state::paused))
    return gneiss::error::not_ready;
  const bool terminal = request_id != 0 && scene_phase_terminal(value.phase);
  auto encoded = protocol::encode(terminal ? scene_result : scene_progress, value,
                                  terminal ? request_id : 0U);
  return encoded ? implementation_->transport.send(*encoded) : encoded.error().error;
}

void runtime_ipc_session::discard_pending_inspection() noexcept {
  if (implementation_)
    implementation_->pending_inspection_frames.clear();
}

gneiss::error
runtime_ipc_session::notify_statistics(const ipc_runtime_statistics& statistics) {
  if (!implementation_ || (implementation_->current_state != runtime_ipc_state::running &&
                           implementation_->current_state != runtime_ipc_state::paused)) {
    return gneiss::error::not_ready;
  }
  gneiss::ipc::envelope envelope;
  const auto operation = make_statistics_event(statistics, envelope);
  return operation == gneiss::error::success ? implementation_->transport.send(envelope)
                                                     : operation;
}

std::size_t runtime_ipc_session::pending_write_count() const noexcept {
  return implementation_ ? implementation_->transport.pending_write_count() : 0U;
}

std::size_t runtime_ipc_session::dropped_event_count() const noexcept {
  return implementation_ ? implementation_->transport.dropped_event_count() : 0U;
}

gneiss::error runtime_ipc_session::stop() {
  if (!implementation_) {
    return gneiss::error::invalid_state;
  }
  if (implementation_->current_state == runtime_ipc_state::stopped) {
    return gneiss::error::success;
  }
  const auto operation = implementation_->transport.stop();
  implementation_->pending_inspection_frames.clear();
  implementation_->current_state = runtime_ipc_state::stopped;
  return operation == gneiss::error::not_ready ? gneiss::error::success : operation;
}

runtime_ipc_state runtime_ipc_session::state() const noexcept {
  return implementation_ ? implementation_->current_state : runtime_ipc_state::failed;
}

bool runtime_ipc_session::supports_domain(std::uint16_t domain) const noexcept {
  return implementation_ && implementation_->negotiated(domain);
}

} // namespace gneiss::host
