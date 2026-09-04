// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_ipc_session.h"

#include "ipc/outbound/runtime_ipc_outbound.h"
#include "ipc/runtime_commands.h"

#include <algorithm>
#include <deque>
#include <optional>
#include <utility>
#include <vector>

namespace gneiss::runtime_internal {

struct runtime_ipc_session::implementation final {
  explicit implementation(runtime_ipc_config value)
      : config(std::move(value)), handshake_deadline(config.handshake_timeout),
        heartbeat_deadline(config.heartbeat_timeout) {
    router_ready = register_runtime_commands(router) == result::success;
  }

  runtime_ipc_config config;
  ipc_transport transport;
  ipc_timeout_tracker handshake_deadline;
  ipc_timeout_tracker heartbeat_deadline;
  runtime_ipc_state current_state = runtime_ipc_state::stopped;
  std::vector<ipc_domain_capability> requested_domains{ipc_v2_domain_capabilities().begin(),
                                                       ipc_v2_domain_capabilities().end()};
  std::vector<ipc_domain_capability> negotiated_domains;
  runtime_command_router router;
  bool router_ready = false;
  bool wants_running = false;
  std::deque<ipc_envelope> pending_inspection_frames;

  result dispatch(const ipc_envelope& envelope, runtime_ipc_actions& actions) noexcept {
    if (!router_ready) {
      return result::invalid_state;
    }
    const ipc_dispatch_context dispatch_context{.remote_role = ipc_peer_role::editor,
                                                .handshake_complete = true,
                                                .negotiated_domains = negotiated_domains};
    runtime_command_context command_context(current_state, actions, this, send_from_command);
    const auto outcome = router.dispatch(envelope, dispatch_context, command_context);
    if (!outcome.accepted()) {
      return outcome.rejection == ipc_dispatch_rejection::handler_failed ? outcome.handler_result
                                                                         : result::invalid_argument;
    }
    return result::success;
  }

  static result send_from_command(void* context, ipc_envelope envelope) noexcept {
    return static_cast<implementation*>(context)->send(std::move(envelope));
  }

  result flush_inspection() noexcept {
    while (!pending_inspection_frames.empty() && transport.pending_write_count() < 48U) {
      const auto operation = transport.send(pending_inspection_frames.front());
      if (operation == result::not_ready) {
        return result::success;
      }
      if (operation != result::success) {
        return operation;
      }
      pending_inspection_frames.pop_front();
    }
    return result::success;
  }

  result send(ipc_envelope envelope) noexcept { return transport.send(envelope); }

  [[nodiscard]] bool negotiated(ipc_domain domain) const noexcept {
    return std::ranges::find(negotiated_domains, domain, &ipc_domain_capability::domain) !=
           negotiated_domains.end();
  }

  result send_state(ipc_control_state state) noexcept {
    ipc_envelope envelope;
    const auto operation = make_state_event(state, envelope);
    return operation == result::success ? send(std::move(envelope)) : operation;
  }

  result enter_running() noexcept {
    ipc_envelope ready;
    auto operation = make_ready_event(ready);
    if (operation == result::success) {
      operation = send(std::move(ready));
    }
    if (operation == result::success) {
      operation = send_state(ipc_control_state::running);
    }
    if (operation == result::success) {
      current_state = runtime_ipc_state::running;
    }
    return operation;
  }

  result fail(result operation, runtime_ipc_actions& actions) noexcept {
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

result runtime_ipc_session::start(clock::time_point now) noexcept {
  if (!implementation_ || implementation_->current_state != runtime_ipc_state::stopped ||
      implementation_->config.session_token.empty()) {
    return result::invalid_state;
  }
  const auto operation = implementation_->transport.start_client(implementation_->config.endpoint);
  if (operation != result::success) {
    implementation_->current_state = runtime_ipc_state::failed;
    return operation;
  }
  implementation_->handshake_deadline.reset(now);
  implementation_->current_state = runtime_ipc_state::connecting;
  return result::success;
}

result runtime_ipc_session::pump(clock::time_point now, runtime_ipc_actions& actions) noexcept {
  actions = {};
  if (!implementation_ || implementation_->current_state == runtime_ipc_state::stopped ||
      implementation_->current_state == runtime_ipc_state::failed) {
    return result::invalid_state;
  }
  std::vector<ipc_transport_event> events;
  (void)implementation_->transport.poll_events(events);
  for (auto& event : events) {
    if (event.type == ipc_transport_event_type::connected) {
      ipc_envelope hello;
      auto operation = make_session_hello_event(implementation_->config.session_token,
                                                implementation_->requested_domains, 1U, hello);
      if (operation == result::success) {
        operation = implementation_->transport.send(hello);
      }
      if (operation != result::success) {
        return implementation_->fail(operation, actions);
      }
      implementation_->current_state = runtime_ipc_state::authenticating;
      continue;
    }
    if (event.type == ipc_transport_event_type::disconnected ||
        event.type == ipc_transport_event_type::error) {
      const auto operation = event.operation == result::success ? result::io : event.operation;
      return implementation_->fail(operation, actions);
    }
    if (event.type != ipc_transport_event_type::envelope_received) {
      continue;
    }

    if (implementation_->current_state == runtime_ipc_state::authenticating) {
      ipc_session_hello hello;
      const auto operation = decode_ipc_session_hello(event.envelope, hello);
      if (operation != result::success || event.envelope.domain != ipc_domain::session ||
          event.envelope.operation != static_cast<std::uint16_t>(ipc_session_operation::hello) ||
          event.envelope.kind != ipc_message_kind::response || event.envelope.request_id != 1U) {
        return implementation_->fail(
            operation == result::success ? result::invalid_argument : operation, actions);
      }
      implementation_->negotiated_domains = std::move(hello.domains);
      if (std::ranges::any_of(implementation_->negotiated_domains, [&](const auto negotiated) {
            const auto requested =
                std::ranges::find(implementation_->requested_domains, negotiated.domain,
                                  &ipc_domain_capability::domain);
            return requested == implementation_->requested_domains.end() ||
                   negotiated.version == 0U || negotiated.version > requested->version;
          })) {
        return implementation_->fail(result::invalid_argument, actions);
      }
      if (!implementation_->negotiated(ipc_domain::control)) {
        return implementation_->fail(result::unsupported, actions);
      }
      implementation_->heartbeat_deadline.reset(now);
      if (implementation_->wants_running) {
        const auto running = implementation_->enter_running();
        if (running != result::success) {
          return implementation_->fail(running, actions);
        }
      }
      continue;
    }

    const auto handled = implementation_->dispatch(event.envelope, actions);
    if (handled != result::success) {
      return implementation_->fail(handled, actions);
    }
    implementation_->heartbeat_deadline.reset(now);
  }

  if ((implementation_->current_state == runtime_ipc_state::connecting ||
       implementation_->current_state == runtime_ipc_state::authenticating) &&
      implementation_->handshake_deadline.expired(now)) {
    return implementation_->fail(result::not_ready, actions);
  }
  if ((implementation_->current_state == runtime_ipc_state::running ||
       implementation_->current_state == runtime_ipc_state::paused) &&
      implementation_->heartbeat_deadline.expired(now)) {
    return implementation_->fail(result::not_ready, actions);
  }
  if (implementation_->current_state == runtime_ipc_state::running ||
      implementation_->current_state == runtime_ipc_state::paused) {
    const auto flushed = implementation_->flush_inspection();
    if (flushed != result::success) {
      return implementation_->fail(flushed, actions);
    }
  }
  return result::success;
}

result runtime_ipc_session::notify_running() noexcept {
  if (!implementation_ || implementation_->current_state == runtime_ipc_state::failed ||
      implementation_->current_state == runtime_ipc_state::stopped) {
    return result::invalid_state;
  }
  implementation_->wants_running = true;
  if (implementation_->current_state == runtime_ipc_state::authenticating ||
      implementation_->current_state == runtime_ipc_state::connecting) {
    return result::success;
  }
  return implementation_->enter_running();
}

result runtime_ipc_session::notify_shutdown(std::int32_t exit_code) noexcept {
  if (!implementation_ || implementation_->current_state == runtime_ipc_state::stopped ||
      implementation_->current_state == runtime_ipc_state::failed) {
    return result::invalid_state;
  }
  implementation_->current_state = runtime_ipc_state::stopping;
  auto operation = implementation_->send_state(ipc_control_state::stopping);
  if (operation == result::success) {
    ipc_envelope envelope;
    operation = make_shutdown_event(exit_code, envelope);
    if (operation == result::success) {
      operation = implementation_->send(std::move(envelope));
    }
  }
  return operation;
}

result runtime_ipc_session::notify_log_event(const gneiss_log_event& event) noexcept {
  if (!implementation_ || (implementation_->current_state != runtime_ipc_state::running &&
                           implementation_->current_state != runtime_ipc_state::paused)) {
    return result::not_ready;
  }
  ipc_envelope envelope;
  const auto operation = make_log_event(event, envelope);
  return operation == result::success ? implementation_->send(std::move(envelope)) : operation;
}

result runtime_ipc_session::notify_scene_snapshot(const ipc_inspection_batch& batch) noexcept {
  if (!implementation_ || !implementation_->negotiated(ipc_domain::inspection) ||
      (implementation_->current_state != runtime_ipc_state::running &&
       implementation_->current_state != runtime_ipc_state::paused)) {
    return result::not_ready;
  }
  if (!implementation_->pending_inspection_frames.empty()) {
    return result::not_ready;
  }
  std::vector<ipc_envelope> frames;
  const auto operation = make_scene_snapshot_events(batch, frames);
  if (operation != result::success) {
    return operation;
  }
  implementation_->pending_inspection_frames.insert(
      implementation_->pending_inspection_frames.end(), std::make_move_iterator(frames.begin()),
      std::make_move_iterator(frames.end()));
  return implementation_->flush_inspection();
}

result runtime_ipc_session::notify_property_write_result(
    const ipc_property_write_result& response) noexcept {
  if (!implementation_ || !implementation_->negotiated(ipc_domain::property) ||
      (implementation_->current_state != runtime_ipc_state::running &&
       implementation_->current_state != runtime_ipc_state::paused)) {
    return result::not_ready;
  }
  ipc_envelope envelope;
  const auto operation = make_property_write_result_event(
      response, static_cast<std::uint32_t>(response.command_id), envelope);
  return operation == result::success ? implementation_->transport.send(envelope) : operation;
}

result runtime_ipc_session::notify_asset_reload_result(const ipc_asset_reload_result& response,
                                                       ipc_asset_operation operation,
                                                       std::uint32_t request_id) noexcept {
  if (!implementation_ || !implementation_->negotiated(ipc_domain::asset) ||
      (implementation_->current_state != runtime_ipc_state::running &&
       implementation_->current_state != runtime_ipc_state::paused)) {
    return result::not_ready;
  }
  ipc_envelope envelope;
  const auto encoded = make_asset_reload_result(response, operation, request_id, envelope);
  return encoded == result::success ? implementation_->transport.send(envelope) : encoded;
}

result runtime_ipc_session::notify_statistics(const ipc_runtime_statistics& statistics) noexcept {
  if (!implementation_ || (implementation_->current_state != runtime_ipc_state::running &&
                           implementation_->current_state != runtime_ipc_state::paused)) {
    return result::not_ready;
  }
  ipc_envelope envelope;
  const auto operation = make_statistics_event(statistics, envelope);
  return operation == result::success ? implementation_->transport.send(envelope) : operation;
}

std::size_t runtime_ipc_session::pending_write_count() const noexcept {
  return implementation_ ? implementation_->transport.pending_write_count() : 0U;
}

std::size_t runtime_ipc_session::dropped_event_count() const noexcept {
  return implementation_ ? implementation_->transport.dropped_event_count() : 0U;
}

result runtime_ipc_session::stop() noexcept {
  if (!implementation_) {
    return result::invalid_state;
  }
  if (implementation_->current_state == runtime_ipc_state::stopped) {
    return result::success;
  }
  const auto operation = implementation_->transport.stop();
  implementation_->pending_inspection_frames.clear();
  implementation_->current_state = runtime_ipc_state::stopped;
  return operation == result::not_ready ? result::success : operation;
}

runtime_ipc_state runtime_ipc_session::state() const noexcept {
  return implementation_ ? implementation_->current_state : runtime_ipc_state::failed;
}

bool runtime_ipc_session::game_updates_enabled() const noexcept {
  return implementation_ && implementation_->current_state == runtime_ipc_state::running;
}

bool runtime_ipc_session::supports_domain(ipc_domain domain) const noexcept {
  return implementation_ && implementation_->negotiated(domain);
}

} // namespace gneiss::runtime_internal
