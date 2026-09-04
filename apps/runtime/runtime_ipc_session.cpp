// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_ipc_session.h"

#include "runtime_ipc_command.h"

#include <gneiss/app/runtime_log_protocol.h>

#include <algorithm>
#include <deque>
#include <optional>
#include <utility>
#include <vector>

namespace gneiss::runtime_internal {

namespace {
constexpr std::size_t runtime_property_write_budget = 32U;
}

struct runtime_ipc_session::implementation final {
  explicit implementation(runtime_ipc_config value)
      : config(std::move(value)), handshake_deadline(config.handshake_timeout),
        heartbeat_deadline(config.heartbeat_timeout) {
    router_ready =
        router.bind(ipc_domain::session, decode_runtime_session_command) == result::success &&
        router.bind(ipc_domain::control, decode_runtime_control_command) == result::success &&
        router.bind(ipc_domain::inspection, decode_runtime_inspection_command) == result::success &&
        router.bind(ipc_domain::property, decode_runtime_property_command) == result::success;
  }

  runtime_ipc_config config;
  ipc_transport transport;
  ipc_timeout_tracker handshake_deadline;
  ipc_timeout_tracker heartbeat_deadline;
  runtime_ipc_state current_state = runtime_ipc_state::stopped;
  std::vector<ipc_domain_capability> requested_domains{ipc_v2_domain_capabilities().begin(),
                                                       ipc_v2_domain_capabilities().end()};
  std::vector<ipc_domain_capability> negotiated_domains;
  ipc_router<runtime_ipc_command> router;
  bool router_ready = false;
  bool wants_running = false;
  std::deque<ipc_envelope> pending_inspection_frames;

  result dispatch(const ipc_envelope& envelope, bool authenticated,
                  runtime_ipc_command& output) noexcept {
    if (!router_ready) {
      return result::invalid_state;
    }
    const ipc_dispatch_context context{.remote_role = ipc_peer_role::editor,
                                       .handshake_complete = authenticated,
                                       .negotiated_domains = negotiated_domains};
    auto routed = router.dispatch(envelope, context);
    if (!routed.accepted()) {
      return routed.outcome.rejection == ipc_dispatch_rejection::handler_failed
                 ? routed.outcome.handler_result
                 : result::invalid_argument;
    }
    output = std::move(*routed.message);
    return result::success;
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
    const auto operation = encode_ipc_control_state(state, envelope);
    return operation == result::success ? send(std::move(envelope)) : operation;
  }

  result send_protocol_error(result operation, std::string text,
                             std::uint32_t request_id) noexcept {
    ipc_envelope envelope;
    const auto encoded = encode_ipc_session_error(
        {.code = to_native(operation), .message = std::move(text)}, request_id, envelope);
    return encoded == result::success ? send(std::move(envelope)) : encoded;
  }

  result enter_running() noexcept {
    ipc_envelope ready;
    auto operation = encode_ipc_control_ready(ready);
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

  result handle_command(const runtime_ipc_command& command, runtime_ipc_actions& actions) noexcept {
    switch (command.kind) {
    case runtime_ipc_command_kind::pause:
      if (current_state != runtime_ipc_state::running) {
        (void)send_protocol_error(result::invalid_state, "当前状态不能暂停", command.request_id);
        return result::success;
      }
      current_state = runtime_ipc_state::paused;
      actions.pause_game = true;
      return send_state(ipc_control_state::paused);
    case runtime_ipc_command_kind::resume:
      if (current_state != runtime_ipc_state::paused) {
        (void)send_protocol_error(result::invalid_state, "当前状态不能恢复", command.request_id);
        return result::success;
      }
      current_state = runtime_ipc_state::running;
      actions.resume_game = true;
      return send_state(ipc_control_state::running);
    case runtime_ipc_command_kind::inspection_resync:
      actions.request_inspection_resync = true;
      return result::success;
    case runtime_ipc_command_kind::stop:
      if (current_state == runtime_ipc_state::stopping) {
        return result::success;
      }
      current_state = runtime_ipc_state::stopping;
      actions.request_exit = true;
      return send_state(ipc_control_state::stopping);
    case runtime_ipc_command_kind::heartbeat: {
      ipc_envelope response;
      const auto operation =
          encode_ipc_session_heartbeat(command.heartbeat, true, command.request_id, response);
      return operation == result::success ? send(std::move(response)) : operation;
    }
    case runtime_ipc_command_kind::property_write:
      if (actions.property_writes.size() >= runtime_property_write_budget) {
        const ipc_property_write_result response{.session_id = command.property.session_id,
                                                 .command_id = command.property.command_id,
                                                 .code = GNEISS_ERROR_NOT_READY,
                                                 .revision = 0U,
                                                 .message = "本帧属性写入队列已满",
                                                 .canonical_value = {}};
        ipc_envelope envelope;
        const auto operation =
            encode_ipc_property_result_v2(response, command.request_id, envelope);
        return operation == result::success ? send(std::move(envelope)) : operation;
      }
      actions.property_writes.push_back(command.property);
      return result::success;
    case runtime_ipc_command_kind::protocol_error:
      return from_native(command.error.code);
    case runtime_ipc_command_kind::hello_acknowledgment:
      return result::invalid_state;
    default:
      return result::unsupported;
    }
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
      auto operation = encode_ipc_session_hello({.token = implementation_->config.session_token,
                                                 .domains = implementation_->requested_domains},
                                                false, 1U, hello);
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
      runtime_ipc_command command;
      const auto operation = implementation_->dispatch(event.envelope, false, command);
      if (operation != result::success ||
          command.kind != runtime_ipc_command_kind::hello_acknowledgment ||
          event.envelope.request_id != 1U) {
        return implementation_->fail(
            operation == result::success ? result::invalid_argument : operation, actions);
      }
      implementation_->negotiated_domains = std::move(command.hello.domains);
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

    runtime_ipc_command command;
    const auto decoded = implementation_->dispatch(event.envelope, true, command);
    if (decoded != result::success) {
      return implementation_->fail(decoded, actions);
    }
    implementation_->heartbeat_deadline.reset(now);
    const auto handled = implementation_->handle_command(command, actions);
    if (handled != result::success) {
      return implementation_->fail(handled, actions);
    }
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
    operation = encode_ipc_session_shutdown({.exit_code = exit_code}, envelope);
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
  try {
    std::string encoded_event;
    auto operation = app::encode_runtime_log_event(event, encoded_event);
    ipc_envelope envelope;
    if (operation == result::success) {
      operation = encode_ipc_log_event(encoded_event, envelope);
    }
    return operation == result::success ? implementation_->send(std::move(envelope)) : operation;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
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
  const auto operation = encode_ipc_inspection_batch_v2(batch, frames);
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
  const auto operation = encode_ipc_property_result_v2(
      response, static_cast<std::uint32_t>(response.command_id), envelope);
  return operation == result::success ? implementation_->transport.send(envelope) : operation;
}

result runtime_ipc_session::notify_statistics(const ipc_runtime_statistics& statistics) noexcept {
  if (!implementation_ || (implementation_->current_state != runtime_ipc_state::running &&
                           implementation_->current_state != runtime_ipc_state::paused)) {
    return result::not_ready;
  }
  ipc_envelope envelope;
  const auto operation = encode_ipc_statistics_v2(statistics, envelope);
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
