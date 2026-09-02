// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_ipc_session.h"

#include <gneiss/app/runtime_log_protocol.h>

#include <algorithm>
#include <utility>
#include <vector>

namespace gneiss::runtime_internal {

struct runtime_ipc_session::implementation final {
  explicit implementation(runtime_ipc_config value)
      : config(std::move(value)), handshake_deadline(config.handshake_timeout),
        heartbeat_deadline(config.heartbeat_timeout) {}

  runtime_ipc_config config;
  ipc_transport transport;
  ipc_timeout_tracker handshake_deadline;
  ipc_timeout_tracker heartbeat_deadline;
  runtime_ipc_state current_state = runtime_ipc_state::stopped;
  std::vector<std::string> requested_capabilities{
      "control", "heartbeat", "logs", std::string(ipc_capability_runtime_inspection_v1)};
  std::vector<std::string> negotiated_capabilities;
  bool wants_running = false;

  result send(ipc_message message) noexcept {
    ipc_frame frame;
    const auto operation = encode_ipc_message(message, frame);
    return operation == result::success ? transport.send(frame) : operation;
  }

  result send_state(ipc_runtime_state state) noexcept {
    ipc_message message;
    message.type = ipc_message_type::state_changed;
    message.runtime_state = state;
    return send(std::move(message));
  }

  result send_protocol_error(result operation, std::string text) noexcept {
    ipc_message message;
    message.type = ipc_message_type::error;
    message.code = to_native(operation);
    message.text = std::move(text);
    return send(std::move(message));
  }

  result enter_running() noexcept {
    ipc_message ready;
    ready.type = ipc_message_type::ready;
    auto operation = send(std::move(ready));
    if (operation == result::success) {
      operation = send_state(ipc_runtime_state::running);
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

  result handle_control(const ipc_message& message, runtime_ipc_actions& actions) noexcept {
    switch (message.type) {
    case ipc_message_type::pause:
      if (current_state != runtime_ipc_state::running) {
        (void)send_protocol_error(result::invalid_state, "当前状态不能暂停");
        return result::success;
      }
      current_state = runtime_ipc_state::paused;
      actions.pause_game = true;
      return send_state(ipc_runtime_state::paused);
    case ipc_message_type::resume:
      if (current_state != runtime_ipc_state::paused) {
        (void)send_protocol_error(result::invalid_state, "当前状态不能恢复");
        return result::success;
      }
      current_state = runtime_ipc_state::running;
      actions.resume_game = true;
      return send_state(ipc_runtime_state::running);
    case ipc_message_type::stop:
      if (current_state == runtime_ipc_state::stopping) {
        return result::success;
      }
      current_state = runtime_ipc_state::stopping;
      actions.request_exit = true;
      return send_state(ipc_runtime_state::stopping);
    case ipc_message_type::ping: {
      ipc_message pong;
      pong.type = ipc_message_type::pong;
      pong.nonce = message.nonce;
      return send(std::move(pong));
    }
    case ipc_message_type::pong:
      return result::success;
    default:
      (void)send_protocol_error(result::invalid_state, "Runtime 收到方向不合法的 IPC 消息");
      return result::success;
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
      ipc_frame hello;
      auto operation = make_ipc_hello(implementation_->config.session_token,
                                      implementation_->requested_capabilities, hello);
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
    if (event.type != ipc_transport_event_type::frame_received) {
      continue;
    }

    if (implementation_->current_state == runtime_ipc_state::authenticating) {
      const auto operation =
          accept_ipc_hello_ack(event.frame, implementation_->requested_capabilities,
                               implementation_->negotiated_capabilities);
      if (operation != result::success) {
        return implementation_->fail(operation, actions);
      }
      if (std::ranges::find(implementation_->negotiated_capabilities, "control") ==
              implementation_->negotiated_capabilities.end() ||
          std::ranges::find(implementation_->negotiated_capabilities, "heartbeat") ==
              implementation_->negotiated_capabilities.end()) {
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

    ipc_message message;
    const auto decoded = decode_ipc_message(event.frame, message);
    if (decoded == result::unsupported) {
      continue;
    }
    if (decoded != result::success) {
      return implementation_->fail(decoded, actions);
    }
    implementation_->heartbeat_deadline.reset(now);
    const auto handled = implementation_->handle_control(message, actions);
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
  auto operation = implementation_->send_state(ipc_runtime_state::stopping);
  if (operation == result::success) {
    ipc_message message;
    message.type = ipc_message_type::shutdown_complete;
    message.code = exit_code;
    operation = implementation_->send(std::move(message));
  }
  return operation;
}

result runtime_ipc_session::notify_log_event(const gneiss_log_event& event) noexcept {
  if (!implementation_ || (implementation_->current_state != runtime_ipc_state::running &&
                           implementation_->current_state != runtime_ipc_state::paused)) {
    return result::not_ready;
  }
  try {
    ipc_message message;
    message.type = ipc_message_type::log_event;
    const auto operation = app::encode_runtime_log_event(event, message.text);
    return operation == result::success ? implementation_->send(std::move(message)) : operation;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

result runtime_ipc_session::notify_scene_snapshot(const ipc_inspection_batch& batch) noexcept {
  if (!implementation_ ||
      std::ranges::find(implementation_->negotiated_capabilities,
                        ipc_capability_runtime_inspection_v1) ==
          implementation_->negotiated_capabilities.end() ||
      (implementation_->current_state != runtime_ipc_state::running &&
       implementation_->current_state != runtime_ipc_state::paused)) {
    return result::not_ready;
  }
  ipc_frame frame;
  const auto operation = encode_ipc_inspection_batch(batch, frame);
  return operation == result::success ? implementation_->transport.send(frame) : operation;
}

result runtime_ipc_session::notify_statistics(const ipc_runtime_statistics& statistics) noexcept {
  if (!implementation_ || (implementation_->current_state != runtime_ipc_state::running &&
                           implementation_->current_state != runtime_ipc_state::paused)) {
    return result::not_ready;
  }
  ipc_frame frame;
  const auto operation = encode_ipc_runtime_statistics(statistics, frame);
  return operation == result::success ? implementation_->transport.send(frame) : operation;
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
  implementation_->current_state = runtime_ipc_state::stopped;
  return operation == result::not_ready ? result::success : operation;
}

runtime_ipc_state runtime_ipc_session::state() const noexcept {
  return implementation_ ? implementation_->current_state : runtime_ipc_state::failed;
}

bool runtime_ipc_session::game_updates_enabled() const noexcept {
  return implementation_ && implementation_->current_state == runtime_ipc_state::running;
}

const std::vector<std::string>& runtime_ipc_session::negotiated_capabilities() const noexcept {
  static const std::vector<std::string> empty;
  return implementation_ ? implementation_->negotiated_capabilities : empty;
}

} // namespace gneiss::runtime_internal
