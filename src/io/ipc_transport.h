// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SRC_IO_IPC_TRANSPORT_H_
#define GNEISS_SRC_IO_IPC_TRANSPORT_H_

#include "ipc_frame.h"

#include <gneiss/core/result.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gneiss {

struct ipc_endpoint final {
  std::string address;
  std::uint16_t port = 0U;
};

enum class ipc_transport_state : std::uint8_t {
  stopped,
  listening,
  connecting,
  connected,
  stopping,
  failed,
};

enum class ipc_transport_event_type : std::uint8_t {
  listening,
  connected,
  frame_received,
  disconnected,
  error,
};

struct ipc_transport_event final {
  ipc_transport_event_type type = ipc_transport_event_type::error;
  result operation = result::success;
  ipc_frame frame;
};

/** 单连接、本机回环 TCP Transport；除 poll_events() 外生命周期操作由调用方外部同步。 */
class ipc_transport final {
public:
  explicit ipc_transport(std::size_t event_capacity = 256U, std::size_t write_capacity = 64U);
  ~ipc_transport();

  ipc_transport(const ipc_transport&) = delete;
  ipc_transport& operator=(const ipc_transport&) = delete;

  [[nodiscard]] result start_server() noexcept;
  [[nodiscard]] result start_client(const ipc_endpoint& endpoint) noexcept;
  [[nodiscard]] result send(const ipc_frame& frame) noexcept;
  [[nodiscard]] result stop() noexcept;

  [[nodiscard]] std::size_t poll_events(std::vector<ipc_transport_event>& output,
                                        std::size_t max_count = 64U) noexcept;
  [[nodiscard]] ipc_endpoint endpoint() const noexcept;
  [[nodiscard]] ipc_transport_state state() const noexcept;
  [[nodiscard]] std::size_t dropped_event_count() const noexcept;
  [[nodiscard]] std::size_t pending_write_count() const noexcept;

private:
  struct implementation;
  std::unique_ptr<implementation> implementation_;
};

} // namespace gneiss

#endif
