// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/function/ipc/transport.hpp>

#include "platform/io/detail/uv_error.hpp"
#include "platform/io/detail/uv_loop_access.hpp"
#include <gneiss/engine/core/base/error.hpp>
#include <gneiss/engine/platform/io/executor.hpp>

#include <uv.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <future>
#include <limits>
#include <mutex>
#include <new>
#include <span>
#include <utility>

namespace gneiss::ipc {

struct transport::implementation final {
  enum class mode : std::uint8_t { none, server, client };

  struct write_request final {
    uv_write_t request{};
    implementation* owner = nullptr;
    std::vector<std::uint8_t> bytes;
  };

  struct rejected_connection final {
    uv_tcp_t stream{};
  };

  implementation(std::size_t events, std::size_t writes, message_classifier classifier)
      : event_capacity(events), write_capacity(writes), is_discardable(classifier) {}

  io::executor executor;
  uv_tcp_t listener{};
  uv_tcp_t stream{};
  uv_connect_t connect_request{};
  std::array<char, 64U * 1024U> read_buffer{};
  ipc::envelope_decoder envelope_decoder;
  mutable std::mutex event_mutex;
  std::deque<transport_event> events;
  std::size_t event_capacity;
  std::size_t write_capacity;
  message_classifier is_discardable;
  std::atomic_size_t pending_writes = 0U;
  std::atomic_size_t dropped_events = 0U;
  std::atomic<transport_state> current_state = transport_state::stopped;
  ipc::endpoint local_endpoint;
  mode current_mode = mode::none;
  bool listener_initialized = false;
  bool stream_initialized = false;
  bool is_stopping = false;

  bool is_low_priority(const transport_event& event) const noexcept {
    return event.type == transport_event_type::envelope_received && is_discardable &&
           is_discardable(event.envelope);
  }

  void emit(transport_event event) {
    {
      const std::scoped_lock lock(event_mutex);
      if (event_capacity == 0U) {
        dropped_events.fetch_add(1U, std::memory_order_relaxed);
        return;
      }
      if (events.size() >= event_capacity) {
        // 可丢弃消息不能挤掉普通消息，分类由本地配置决定。
        const auto discardable = std::ranges::find_if(
            events, [this](const auto& value) { return is_low_priority(value); });
        if (!is_low_priority(event) && discardable != events.end()) {
          events.erase(discardable);
        } else if (event.type == transport_event_type::envelope_received) {
          dropped_events.fetch_add(1U, std::memory_order_relaxed);
          return;
        } else {
          const auto frame = std::ranges::find_if(events, [](const auto& pending) {
            return pending.type == transport_event_type::envelope_received;
          });
          if (frame != events.end()) {
            events.erase(frame);
          } else {
            events.pop_front();
          }
        }
        dropped_events.fetch_add(1U, std::memory_order_relaxed);
      }
      events.push_back(std::move(event));
    }
  }

  void emit_status(transport_event_type type, error operation = error::success) {
    transport_event event;
    event.type = type;
    event.operation = operation;
    emit(std::move(event));
  }

  static void allocate_read_buffer(uv_handle_t* handle, std::size_t, uv_buf_t* buffer) noexcept
      try {
    auto* self = static_cast<implementation*>(handle->data);
    *buffer =
        uv_buf_init(self->read_buffer.data(), static_cast<unsigned int>(self->read_buffer.size()));
  } catch (...) {
    gneiss::fatal_current_exception();
  }

  static void on_stream_closed(uv_handle_t* handle) noexcept try {
    auto* self = static_cast<implementation*>(handle->data);
    self->stream_initialized = false;
    self->envelope_decoder.reset();
    if (!self->is_stopping && self->current_mode == mode::server && self->listener_initialized) {
      self->current_state.store(transport_state::listening, std::memory_order_release);
    }
  } catch (...) {
    gneiss::fatal_current_exception();
  }

  void close_stream() {
    if (stream_initialized && uv_is_closing(reinterpret_cast<uv_handle_t*>(&stream)) == 0) {
      (void)uv_read_stop(reinterpret_cast<uv_stream_t*>(&stream));
      uv_close(reinterpret_cast<uv_handle_t*>(&stream), on_stream_closed);
    }
  }

  static void on_read(uv_stream_t* source, ssize_t count, const uv_buf_t* buffer) noexcept try {
    auto* self = static_cast<implementation*>(source->data);
    if (count > 0) {
      const auto bytes = std::span(reinterpret_cast<const std::uint8_t*>(buffer->base),
                                   static_cast<std::size_t>(count));
      std::vector<envelope> envelopes;
      const auto operation = self->envelope_decoder.append(bytes, envelopes);
      if (operation == error::success) {
        for (auto& envelope : envelopes) {
          transport_event event;
          event.type = transport_event_type::envelope_received;
          event.envelope = std::move(envelope);
          self->emit(std::move(event));
        }
      }
      if (operation != error::success) {
        self->emit_status(transport_event_type::error, operation);
        self->current_state.store(transport_state::failed, std::memory_order_release);
        self->close_stream();
        return;
      }
      return;
    }
    if (count < 0) {
      const auto operation = count == UV_EOF
                                 ? error::success
                                 : gneiss::io::detail::from_uv_status(static_cast<int>(count));
      if (!self->is_stopping) {
        self->emit_status(transport_event_type::disconnected, operation);
      }
      self->close_stream();
    }
  } catch (...) {
    gneiss::fatal_current_exception();
  }

  error begin_read() {
    const auto operation = gneiss::io::detail::from_uv_status(
        uv_read_start(reinterpret_cast<uv_stream_t*>(&stream), allocate_read_buffer, on_read));
    if (operation == error::success) {
      current_state.store(transport_state::connected, std::memory_order_release);
      emit_status(transport_event_type::connected);
    }
    return operation;
  }

  static void delete_rejected_connection(uv_handle_t* handle) noexcept try {
    delete reinterpret_cast<rejected_connection*>(handle);
  } catch (...) {
    gneiss::fatal_current_exception();
  }

  static void on_connection(uv_stream_t* server, int status) noexcept try {
    auto* self = static_cast<implementation*>(server->data);
    if (status < 0 || self->is_stopping) {
      if (status < 0 && !self->is_stopping) {
        self->emit_status(transport_event_type::error, gneiss::io::detail::from_uv_status(status));
      }
      return;
    }
    if (self->stream_initialized) {
      auto* rejected = new rejected_connection;
      const auto initialized =
          gneiss::io::detail::from_uv_status(uv_tcp_init(server->loop, &rejected->stream));
      if (initialized.failed()) {
        delete rejected;
        self->emit_status(transport_event_type::error, initialized);
        return;
      }
      if (uv_accept(server, reinterpret_cast<uv_stream_t*>(&rejected->stream)) != 0) {
        uv_close(reinterpret_cast<uv_handle_t*>(&rejected->stream), delete_rejected_connection);
        return;
      }
      uv_close(reinterpret_cast<uv_handle_t*>(&rejected->stream), delete_rejected_connection);
      return;
    }

    const auto initialized =
        gneiss::io::detail::from_uv_status(uv_tcp_init(server->loop, &self->stream));
    if (initialized != error::success) {
      self->emit_status(transport_event_type::error, initialized);
      return;
    }
    self->stream_initialized = true;
    self->stream.data = self;
    const auto accepted = gneiss::io::detail::from_uv_status(
        uv_accept(server, reinterpret_cast<uv_stream_t*>(&self->stream)));
    if (accepted != error::success) {
      self->emit_status(transport_event_type::error, accepted);
      self->close_stream();
      return;
    }
    const auto reading = self->begin_read();
    if (reading != error::success) {
      self->emit_status(transport_event_type::error, reading);
      self->close_stream();
    }
  } catch (...) {
    gneiss::fatal_current_exception();
  }

  static void on_connect(uv_connect_t* request, int status) noexcept try {
    auto* self = static_cast<implementation*>(request->data);
    if (self->is_stopping) {
      return;
    }
    if (status < 0) {
      self->current_state.store(transport_state::failed, std::memory_order_release);
      self->emit_status(transport_event_type::error, gneiss::io::detail::from_uv_status(status));
      self->close_stream();
      return;
    }
    const auto reading = self->begin_read();
    if (reading != error::success) {
      self->current_state.store(transport_state::failed, std::memory_order_release);
      self->emit_status(transport_event_type::error, reading);
      self->close_stream();
    }
  } catch (...) {
    gneiss::fatal_current_exception();
  }

  static void on_write(uv_write_t* request, int status) noexcept try {
    auto* write = static_cast<write_request*>(request->data);
    write->owner->pending_writes.fetch_sub(1U, std::memory_order_release);
    if (status < 0 && !write->owner->is_stopping) {
      write->owner->emit_status(transport_event_type::error,
                                gneiss::io::detail::from_uv_status(status));
    }
    delete write;
  } catch (...) {
    gneiss::fatal_current_exception();
  }

  void write(std::vector<std::uint8_t> bytes) {
    if (!stream_initialized || uv_is_closing(reinterpret_cast<uv_handle_t*>(&stream)) != 0) {
      pending_writes.fetch_sub(1U, std::memory_order_release);
      emit_status(transport_event_type::error, error::not_ready);
      return;
    }
    auto* write = new write_request;
    write->owner = this;
    write->bytes = std::move(bytes);
    write->request.data = write;
    auto buffer = uv_buf_init(reinterpret_cast<char*>(write->bytes.data()),
                              static_cast<unsigned int>(write->bytes.size()));
    const auto operation =
        uv_write(&write->request, reinterpret_cast<uv_stream_t*>(&stream), &buffer, 1U, on_write);
    if (operation != 0) {
      pending_writes.fetch_sub(1U, std::memory_order_release);
      emit_status(transport_event_type::error, gneiss::io::detail::from_uv_status(operation));
      delete write;
    }
  }

  error enqueue(std::vector<std::uint8_t> bytes, bool discardable) {
    // 在同一个 CAS 中为非日志消息保留容量，避免并发日志发送占满控制通道。
    const auto limit =
        write_capacity - (discardable ? (std::min)(write_capacity / 4U, std::size_t{16U}) : 0U);
    auto pending = pending_writes.load(std::memory_order_relaxed);
    do {
      if (pending >= limit) {
        return error::not_ready;
      }
    } while (!pending_writes.compare_exchange_weak(pending, pending + 1U, std::memory_order_acq_rel,
                                                   std::memory_order_relaxed));

    const auto operation = io::detail::uv_loop_access::post(
        executor, [state = this, data = std::move(bytes)](uv_loop_t*) mutable {
          state->write(std::move(data));
        });
    if (operation != error::success) {
      pending_writes.fetch_sub(1U, std::memory_order_release);
    }
    return operation;
  }

  void reset_for_start(mode selected_mode) {
    const std::scoped_lock lock(event_mutex);
    events.clear();
    dropped_events.store(0U, std::memory_order_relaxed);
    pending_writes.store(0U, std::memory_order_relaxed);
    envelope_decoder.reset();
    current_mode = selected_mode;
    is_stopping = false;
    listener_initialized = false;
    stream_initialized = false;
    local_endpoint = {};
  }
};

transport::transport(std::size_t event_capacity, std::size_t write_capacity,
                     message_classifier classifier)
    : implementation_(
          std::make_unique<implementation>(event_capacity, write_capacity, classifier)) {}

transport::~transport() {
  if (implementation_) {
    (void)stop();
  }
}

error transport::start_server() {
  if (!implementation_ || implementation_->event_capacity == 0U ||
      implementation_->write_capacity == 0U) {
    return error::invalid_argument;
  }
  if (implementation_->current_state.load(std::memory_order_acquire) != transport_state::stopped) {
    return error::invalid_state;
  }
  implementation_->reset_for_start(implementation::mode::server);
  auto operation = implementation_->executor.start();
  if (operation != error::success) {
    return operation;
  }

  {
    auto completion = std::make_shared<std::promise<error>>();
    auto completed = completion->get_future();
    operation = io::detail::uv_loop_access::post(
        implementation_->executor, [state = implementation_.get(), completion](uv_loop_t* loop) {
          auto current = gneiss::io::detail::from_uv_status(uv_tcp_init(loop, &state->listener));
          if (current == error::success) {
            state->listener_initialized = true;
            state->listener.data = state;
            sockaddr_in address{};
            current = gneiss::io::detail::from_uv_status(uv_ip4_addr("127.0.0.1", 0, &address));
            if (current == error::success) {
              current = gneiss::io::detail::from_uv_status(
                  uv_tcp_bind(&state->listener, reinterpret_cast<const sockaddr*>(&address), 0U));
            }
            if (current == error::success) {
              current = gneiss::io::detail::from_uv_status(
                  uv_listen(reinterpret_cast<uv_stream_t*>(&state->listener), 1,
                            implementation::on_connection));
            }
            if (current == error::success) {
              sockaddr_in local{};
              int length = sizeof(local);
              current = gneiss::io::detail::from_uv_status(uv_tcp_getsockname(
                  &state->listener, reinterpret_cast<sockaddr*>(&local), &length));
              if (current == error::success) {
                state->local_endpoint = {"127.0.0.1", ntohs(local.sin_port)};
                state->current_state.store(transport_state::listening, std::memory_order_release);
                state->emit_status(transport_event_type::listening);
              }
            }
          }
          if (current != error::success) {
            state->current_state.store(transport_state::failed, std::memory_order_release);
            state->emit_status(transport_event_type::error, current);
            if (state->listener_initialized) {
              uv_close(reinterpret_cast<uv_handle_t*>(&state->listener), nullptr);
            }
          }
          completion->set_value(current);
        });
    if (operation == error::success) {
      operation = completed.get();
    }
  }
  if (operation != error::success) {
    (void)implementation_->executor.stop();
    implementation_->current_state.store(transport_state::stopped, std::memory_order_release);
  }
  return operation;
}

error transport::start_client(const ipc::endpoint& endpoint) {
  if (!implementation_ || implementation_->event_capacity == 0U ||
      implementation_->write_capacity == 0U || endpoint.address != "127.0.0.1" ||
      endpoint.port == 0U) {
    return error::invalid_argument;
  }
  if (implementation_->current_state.load(std::memory_order_acquire) != transport_state::stopped) {
    return error::invalid_state;
  }
  implementation_->reset_for_start(implementation::mode::client);
  auto operation = implementation_->executor.start();
  if (operation != error::success) {
    return operation;
  }

  {
    auto completion = std::make_shared<std::promise<error>>();
    auto completed = completion->get_future();
    operation = io::detail::uv_loop_access::post(
        implementation_->executor,
        [state = implementation_.get(), endpoint, completion](uv_loop_t* loop) {
          auto current = gneiss::io::detail::from_uv_status(uv_tcp_init(loop, &state->stream));
          if (current == error::success) {
            state->stream_initialized = true;
            state->stream.data = state;
            state->connect_request.data = state;
            sockaddr_in address{};
            current = gneiss::io::detail::from_uv_status(
                uv_ip4_addr(endpoint.address.c_str(), endpoint.port, &address));
            if (current == error::success) {
              current = gneiss::io::detail::from_uv_status(uv_tcp_connect(
                  &state->connect_request, &state->stream,
                  reinterpret_cast<const sockaddr*>(&address), implementation::on_connect));
            }
          }
          if (current == error::success) {
            state->current_state.store(transport_state::connecting, std::memory_order_release);
          } else {
            state->current_state.store(transport_state::failed, std::memory_order_release);
            state->emit_status(transport_event_type::error, current);
            state->close_stream();
          }
          completion->set_value(current);
        });
    if (operation == error::success) {
      operation = completed.get();
    }
  }
  if (operation != error::success) {
    (void)implementation_->executor.stop();
    implementation_->current_state.store(transport_state::stopped, std::memory_order_release);
  }
  return operation;
}

error transport::send(const envelope& envelope) {
  if (!implementation_ || implementation_->current_state.load(std::memory_order_acquire) !=
                              transport_state::connected) {
    return error::not_ready;
  }
  std::vector<std::uint8_t> encoded;
  const auto operation = encode_envelope(envelope, encoded);
  return operation == error::success
             ? implementation_->enqueue(std::move(encoded),
                                        implementation_->is_discardable &&
                                            implementation_->is_discardable(envelope))
             : operation;
}

std::size_t transport::pending_write_count() const noexcept {
  return implementation_ ? implementation_->pending_writes.load(std::memory_order_acquire) : 0U;
}

error transport::stop() {
  if (!implementation_ || !implementation_->executor.is_running()) {
    return error::not_ready;
  }
  implementation_->current_state.store(transport_state::stopping, std::memory_order_release);
  auto operation = io::detail::uv_loop_access::post(
      implementation_->executor, [state = implementation_.get()](uv_loop_t*) {
        state->is_stopping = true;
        state->close_stream();
        if (state->listener_initialized &&
            uv_is_closing(reinterpret_cast<uv_handle_t*>(&state->listener)) == 0) {
          uv_close(reinterpret_cast<uv_handle_t*>(&state->listener), nullptr);
        }
      });
  if (operation != error::success) {
    return operation;
  }
  operation = implementation_->executor.stop();
  implementation_->current_state.store(transport_state::stopped, std::memory_order_release);
  implementation_->current_mode = implementation::mode::none;
  implementation_->listener_initialized = false;
  implementation_->stream_initialized = false;
  return operation;
}

std::size_t transport::drain_events(std::vector<transport_event>& output, std::size_t max_count) {
  if (!implementation_ || max_count == 0U) {
    return 0U;
  }
  {
    const std::scoped_lock lock(implementation_->event_mutex);
    const auto count = (std::min)(max_count, implementation_->events.size());
    output.reserve(output.size() + count);
    for (std::size_t index = 0U; index < count; ++index) {
      output.push_back(std::move(implementation_->events.front()));
      implementation_->events.pop_front();
    }
    return count;
  }
}

endpoint transport::endpoint() const {
  if (!implementation_) {
    return {};
  }
  {
    return implementation_->local_endpoint;
  }
}

transport_state transport::state() const noexcept {
  return implementation_ ? implementation_->current_state.load(std::memory_order_acquire)
                         : transport_state::failed;
}

std::size_t transport::dropped_event_count() const noexcept {
  return implementation_ ? implementation_->dropped_events.load(std::memory_order_relaxed) : 0U;
}

} // namespace gneiss::ipc
