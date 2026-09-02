// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ipc_transport.h"

#include "uv_error.h"
#include "uv_runtime.h"
#include "uv_runtime_access.h"

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

namespace gneiss {

struct ipc_transport::implementation final {
  enum class mode : std::uint8_t { none, server, client };

  struct write_request final {
    uv_write_t request{};
    implementation* owner = nullptr;
    std::vector<std::uint8_t> bytes;
  };

  struct rejected_connection final {
    uv_tcp_t stream{};
  };

  implementation(std::size_t events, std::size_t writes)
      : event_capacity(events), write_capacity(writes) {}

  uv_runtime runtime;
  uv_tcp_t listener{};
  uv_tcp_t stream{};
  uv_connect_t connect_request{};
  std::array<char, 64U * 1024U> read_buffer{};
  ipc_frame_decoder decoder;
  mutable std::mutex event_mutex;
  std::deque<ipc_transport_event> events;
  std::size_t event_capacity;
  std::size_t write_capacity;
  std::atomic_size_t pending_writes = 0U;
  std::atomic_size_t dropped_events = 0U;
  std::atomic<ipc_transport_state> current_state = ipc_transport_state::stopped;
  ipc_endpoint local_endpoint;
  mode current_mode = mode::none;
  bool listener_initialized = false;
  bool stream_initialized = false;
  bool is_stopping = false;

  void emit(ipc_transport_event event) noexcept {
    try {
      const std::scoped_lock lock(event_mutex);
      if (event_capacity == 0U) {
        dropped_events.fetch_add(1U, std::memory_order_relaxed);
        return;
      }
      if (events.size() >= event_capacity) {
        if (event.type == ipc_transport_event_type::frame_received) {
          dropped_events.fetch_add(1U, std::memory_order_relaxed);
          return;
        }
        const auto frame = std::ranges::find_if(events, [](const auto& pending) {
          return pending.type == ipc_transport_event_type::frame_received;
        });
        if (frame != events.end()) {
          events.erase(frame);
        } else {
          events.pop_front();
        }
        dropped_events.fetch_add(1U, std::memory_order_relaxed);
      }
      events.push_back(std::move(event));
    } catch (...) {
      dropped_events.fetch_add(1U, std::memory_order_relaxed);
    }
  }

  void emit_status(ipc_transport_event_type type, result operation = result::success) noexcept {
    ipc_transport_event event;
    event.type = type;
    event.operation = operation;
    emit(std::move(event));
  }

  static void allocate_read_buffer(uv_handle_t* handle, std::size_t, uv_buf_t* buffer) noexcept {
    auto* self = static_cast<implementation*>(handle->data);
    *buffer =
        uv_buf_init(self->read_buffer.data(), static_cast<unsigned int>(self->read_buffer.size()));
  }

  static void on_stream_closed(uv_handle_t* handle) noexcept {
    auto* self = static_cast<implementation*>(handle->data);
    self->stream_initialized = false;
    self->decoder.reset();
    if (!self->is_stopping && self->current_mode == mode::server && self->listener_initialized) {
      self->current_state.store(ipc_transport_state::listening, std::memory_order_release);
    }
  }

  void close_stream() noexcept {
    if (stream_initialized && uv_is_closing(reinterpret_cast<uv_handle_t*>(&stream)) == 0) {
      (void)uv_read_stop(reinterpret_cast<uv_stream_t*>(&stream));
      uv_close(reinterpret_cast<uv_handle_t*>(&stream), on_stream_closed);
    }
  }

  static void on_read(uv_stream_t* source, ssize_t count, const uv_buf_t* buffer) noexcept {
    auto* self = static_cast<implementation*>(source->data);
    if (count > 0) {
      std::vector<ipc_frame> frames;
      const auto bytes = std::span(reinterpret_cast<const std::uint8_t*>(buffer->base),
                                   static_cast<std::size_t>(count));
      const auto operation = self->decoder.append(bytes, frames);
      if (operation != result::success) {
        self->emit_status(ipc_transport_event_type::error, operation);
        self->current_state.store(ipc_transport_state::failed, std::memory_order_release);
        self->close_stream();
        return;
      }
      for (auto& frame : frames) {
        ipc_transport_event event;
        event.type = ipc_transport_event_type::frame_received;
        event.frame = std::move(frame);
        self->emit(std::move(event));
      }
      return;
    }
    if (count < 0) {
      const auto operation =
          count == UV_EOF ? result::success : from_uv_error(static_cast<int>(count));
      if (!self->is_stopping) {
        self->emit_status(ipc_transport_event_type::disconnected, operation);
      }
      self->close_stream();
    }
  }

  result begin_read() noexcept {
    const auto operation = from_uv_error(
        uv_read_start(reinterpret_cast<uv_stream_t*>(&stream), allocate_read_buffer, on_read));
    if (operation == result::success) {
      current_state.store(ipc_transport_state::connected, std::memory_order_release);
      emit_status(ipc_transport_event_type::connected);
    }
    return operation;
  }

  static void delete_rejected_connection(uv_handle_t* handle) noexcept {
    delete reinterpret_cast<rejected_connection*>(handle);
  }

  static void on_connection(uv_stream_t* server, int status) noexcept {
    auto* self = static_cast<implementation*>(server->data);
    if (status < 0 || self->is_stopping) {
      if (status < 0 && !self->is_stopping) {
        self->emit_status(ipc_transport_event_type::error, from_uv_error(status));
      }
      return;
    }
    if (self->stream_initialized) {
      auto* rejected = new (std::nothrow) rejected_connection;
      if (rejected == nullptr || uv_tcp_init(server->loop, &rejected->stream) != 0) {
        delete rejected;
        self->emit_status(ipc_transport_event_type::error, result::out_of_memory);
        return;
      }
      if (uv_accept(server, reinterpret_cast<uv_stream_t*>(&rejected->stream)) != 0) {
        uv_close(reinterpret_cast<uv_handle_t*>(&rejected->stream), delete_rejected_connection);
        return;
      }
      uv_close(reinterpret_cast<uv_handle_t*>(&rejected->stream), delete_rejected_connection);
      return;
    }

    const auto initialized = from_uv_error(uv_tcp_init(server->loop, &self->stream));
    if (initialized != result::success) {
      self->emit_status(ipc_transport_event_type::error, initialized);
      return;
    }
    self->stream_initialized = true;
    self->stream.data = self;
    const auto accepted =
        from_uv_error(uv_accept(server, reinterpret_cast<uv_stream_t*>(&self->stream)));
    if (accepted != result::success) {
      self->emit_status(ipc_transport_event_type::error, accepted);
      self->close_stream();
      return;
    }
    const auto reading = self->begin_read();
    if (reading != result::success) {
      self->emit_status(ipc_transport_event_type::error, reading);
      self->close_stream();
    }
  }

  static void on_connect(uv_connect_t* request, int status) noexcept {
    auto* self = static_cast<implementation*>(request->data);
    if (self->is_stopping) {
      return;
    }
    if (status < 0) {
      self->current_state.store(ipc_transport_state::failed, std::memory_order_release);
      self->emit_status(ipc_transport_event_type::error, from_uv_error(status));
      self->close_stream();
      return;
    }
    const auto reading = self->begin_read();
    if (reading != result::success) {
      self->current_state.store(ipc_transport_state::failed, std::memory_order_release);
      self->emit_status(ipc_transport_event_type::error, reading);
      self->close_stream();
    }
  }

  static void on_write(uv_write_t* request, int status) noexcept {
    auto* write = static_cast<write_request*>(request->data);
    write->owner->pending_writes.fetch_sub(1U, std::memory_order_release);
    if (status < 0 && !write->owner->is_stopping) {
      write->owner->emit_status(ipc_transport_event_type::error, from_uv_error(status));
    }
    delete write;
  }

  void write(std::vector<std::uint8_t> bytes) noexcept {
    if (!stream_initialized || uv_is_closing(reinterpret_cast<uv_handle_t*>(&stream)) != 0) {
      pending_writes.fetch_sub(1U, std::memory_order_release);
      emit_status(ipc_transport_event_type::error, result::not_ready);
      return;
    }
    auto* write = new (std::nothrow) write_request;
    if (write == nullptr) {
      pending_writes.fetch_sub(1U, std::memory_order_release);
      emit_status(ipc_transport_event_type::error, result::out_of_memory);
      return;
    }
    write->owner = this;
    write->bytes = std::move(bytes);
    write->request.data = write;
    auto buffer = uv_buf_init(reinterpret_cast<char*>(write->bytes.data()),
                              static_cast<unsigned int>(write->bytes.size()));
    const auto operation =
        uv_write(&write->request, reinterpret_cast<uv_stream_t*>(&stream), &buffer, 1U, on_write);
    if (operation != 0) {
      pending_writes.fetch_sub(1U, std::memory_order_release);
      emit_status(ipc_transport_event_type::error, from_uv_error(operation));
      delete write;
    }
  }

  void reset_for_start(mode selected_mode) noexcept {
    const std::scoped_lock lock(event_mutex);
    events.clear();
    dropped_events.store(0U, std::memory_order_relaxed);
    pending_writes.store(0U, std::memory_order_relaxed);
    decoder.reset();
    current_mode = selected_mode;
    is_stopping = false;
    listener_initialized = false;
    stream_initialized = false;
    local_endpoint = {};
  }
};

ipc_transport::ipc_transport(std::size_t event_capacity, std::size_t write_capacity)
    : implementation_(std::make_unique<implementation>(event_capacity, write_capacity)) {}

ipc_transport::~ipc_transport() {
  if (implementation_) {
    (void)stop();
  }
}

result ipc_transport::start_server() noexcept {
  if (!implementation_ || implementation_->event_capacity == 0U ||
      implementation_->write_capacity == 0U) {
    return result::invalid_argument;
  }
  if (implementation_->current_state.load(std::memory_order_acquire) !=
      ipc_transport_state::stopped) {
    return result::invalid_state;
  }
  implementation_->reset_for_start(implementation::mode::server);
  auto operation = implementation_->runtime.start();
  if (operation != result::success) {
    return operation;
  }

  try {
    auto completion = std::make_shared<std::promise<result>>();
    auto completed = completion->get_future();
    operation = uv_runtime_access::post(implementation_->runtime, [state = implementation_.get(),
                                                                   completion](uv_loop_t* loop) {
      auto current = from_uv_error(uv_tcp_init(loop, &state->listener));
      if (current == result::success) {
        state->listener_initialized = true;
        state->listener.data = state;
        sockaddr_in address{};
        current = from_uv_error(uv_ip4_addr("127.0.0.1", 0, &address));
        if (current == result::success) {
          current = from_uv_error(
              uv_tcp_bind(&state->listener, reinterpret_cast<const sockaddr*>(&address), 0U));
        }
        if (current == result::success) {
          current = from_uv_error(uv_listen(reinterpret_cast<uv_stream_t*>(&state->listener), 1,
                                            implementation::on_connection));
        }
        if (current == result::success) {
          sockaddr_in local{};
          int length = sizeof(local);
          current = from_uv_error(
              uv_tcp_getsockname(&state->listener, reinterpret_cast<sockaddr*>(&local), &length));
          if (current == result::success) {
            state->local_endpoint = {"127.0.0.1", ntohs(local.sin_port)};
            state->current_state.store(ipc_transport_state::listening, std::memory_order_release);
            state->emit_status(ipc_transport_event_type::listening);
          }
        }
      }
      if (current != result::success) {
        state->current_state.store(ipc_transport_state::failed, std::memory_order_release);
        state->emit_status(ipc_transport_event_type::error, current);
        if (state->listener_initialized) {
          uv_close(reinterpret_cast<uv_handle_t*>(&state->listener), nullptr);
        }
      }
      completion->set_value(current);
    });
    if (operation == result::success) {
      operation = completed.get();
    }
  } catch (const std::bad_alloc&) {
    operation = result::out_of_memory;
  } catch (...) {
    operation = result::internal;
  }
  if (operation != result::success) {
    (void)implementation_->runtime.stop();
    implementation_->current_state.store(ipc_transport_state::stopped, std::memory_order_release);
  }
  return operation;
}

result ipc_transport::start_client(const ipc_endpoint& endpoint) noexcept {
  if (!implementation_ || implementation_->event_capacity == 0U ||
      implementation_->write_capacity == 0U || endpoint.address != "127.0.0.1" ||
      endpoint.port == 0U) {
    return result::invalid_argument;
  }
  if (implementation_->current_state.load(std::memory_order_acquire) !=
      ipc_transport_state::stopped) {
    return result::invalid_state;
  }
  implementation_->reset_for_start(implementation::mode::client);
  auto operation = implementation_->runtime.start();
  if (operation != result::success) {
    return operation;
  }

  try {
    auto completion = std::make_shared<std::promise<result>>();
    auto completed = completion->get_future();
    operation =
        uv_runtime_access::post(implementation_->runtime, [state = implementation_.get(), endpoint,
                                                           completion](uv_loop_t* loop) {
          auto current = from_uv_error(uv_tcp_init(loop, &state->stream));
          if (current == result::success) {
            state->stream_initialized = true;
            state->stream.data = state;
            state->connect_request.data = state;
            sockaddr_in address{};
            current = from_uv_error(uv_ip4_addr(endpoint.address.c_str(), endpoint.port, &address));
            if (current == result::success) {
              current = from_uv_error(uv_tcp_connect(&state->connect_request, &state->stream,
                                                     reinterpret_cast<const sockaddr*>(&address),
                                                     implementation::on_connect));
            }
          }
          if (current == result::success) {
            state->current_state.store(ipc_transport_state::connecting, std::memory_order_release);
          } else {
            state->current_state.store(ipc_transport_state::failed, std::memory_order_release);
            state->emit_status(ipc_transport_event_type::error, current);
            state->close_stream();
          }
          completion->set_value(current);
        });
    if (operation == result::success) {
      operation = completed.get();
    }
  } catch (const std::bad_alloc&) {
    operation = result::out_of_memory;
  } catch (...) {
    operation = result::internal;
  }
  if (operation != result::success) {
    (void)implementation_->runtime.stop();
    implementation_->current_state.store(ipc_transport_state::stopped, std::memory_order_release);
  }
  return operation;
}

result ipc_transport::send(const ipc_frame& frame) noexcept {
  if (!implementation_ || implementation_->current_state.load(std::memory_order_acquire) !=
                              ipc_transport_state::connected) {
    return result::not_ready;
  }
  std::vector<std::uint8_t> encoded;
  auto operation = encode_ipc_frame(frame, encoded);
  if (operation != result::success) {
    return operation;
  }

  auto pending = implementation_->pending_writes.load(std::memory_order_relaxed);
  do {
    if (pending >= implementation_->write_capacity) {
      return result::not_ready;
    }
  } while (!implementation_->pending_writes.compare_exchange_weak(
      pending, pending + 1U, std::memory_order_acq_rel, std::memory_order_relaxed));

  operation = uv_runtime_access::post(implementation_->runtime,
                                      [state = implementation_.get(), bytes = std::move(encoded)](
                                          uv_loop_t*) mutable { state->write(std::move(bytes)); });
  if (operation != result::success) {
    implementation_->pending_writes.fetch_sub(1U, std::memory_order_release);
  }
  return operation;
}

std::size_t ipc_transport::pending_write_count() const noexcept {
  return implementation_ ? implementation_->pending_writes.load(std::memory_order_acquire) : 0U;
}

result ipc_transport::stop() noexcept {
  if (!implementation_ || !implementation_->runtime.is_running()) {
    return result::not_ready;
  }
  implementation_->current_state.store(ipc_transport_state::stopping, std::memory_order_release);
  auto operation = uv_runtime_access::post(
      implementation_->runtime, [state = implementation_.get()](uv_loop_t*) {
        state->is_stopping = true;
        state->close_stream();
        if (state->listener_initialized &&
            uv_is_closing(reinterpret_cast<uv_handle_t*>(&state->listener)) == 0) {
          uv_close(reinterpret_cast<uv_handle_t*>(&state->listener), nullptr);
        }
      });
  if (operation != result::success) {
    return operation;
  }
  operation = implementation_->runtime.stop();
  implementation_->current_state.store(ipc_transport_state::stopped, std::memory_order_release);
  implementation_->current_mode = implementation::mode::none;
  implementation_->listener_initialized = false;
  implementation_->stream_initialized = false;
  return operation;
}

std::size_t ipc_transport::poll_events(std::vector<ipc_transport_event>& output,
                                       std::size_t max_count) noexcept {
  if (!implementation_ || max_count == 0U) {
    return 0U;
  }
  try {
    const std::scoped_lock lock(implementation_->event_mutex);
    const auto count = (std::min)(max_count, implementation_->events.size());
    output.reserve(output.size() + count);
    for (std::size_t index = 0U; index < count; ++index) {
      output.push_back(std::move(implementation_->events.front()));
      implementation_->events.pop_front();
    }
    return count;
  } catch (...) {
    return 0U;
  }
}

ipc_endpoint ipc_transport::endpoint() const noexcept {
  if (!implementation_) {
    return {};
  }
  try {
    return implementation_->local_endpoint;
  } catch (...) {
    return {};
  }
}

ipc_transport_state ipc_transport::state() const noexcept {
  return implementation_ ? implementation_->current_state.load(std::memory_order_acquire)
                         : ipc_transport_state::failed;
}

std::size_t ipc_transport::dropped_event_count() const noexcept {
  return implementation_ ? implementation_->dropped_events.load(std::memory_order_relaxed) : 0U;
}

} // namespace gneiss
