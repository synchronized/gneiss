// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <gneiss/engine/function/ipc/envelope.hpp>

#include <gneiss/engine/core/base/error.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gneiss::ipc {

struct endpoint final {
  std::string address;
  std::uint16_t port = 0U;
};

enum class transport_state : std::uint8_t {
  stopped,
  listening,
  connecting,
  connected,
  stopping,
  failed,
};

enum class transport_event_type : std::uint8_t {
  listening,
  connected,
  envelope_received,
  disconnected,
  error,
};

struct transport_event final {
  transport_event_type type = transport_event_type::error;
  error operation = error::success;
  envelope envelope;
};

/** 本地策略在发送与接收线程调用，须为线程安全的纯函数，不借用外部状态。 */
using message_classifier = bool (*)(const envelope&) noexcept;

/** 单连接、本机回环 TCP Transport；除 drain_events() 外生命周期操作由调用方外部同步。
 * 可丢弃消息不占用发送队列最后四分之一（最多 16 项）；接收队列满时普通消息优先替换可丢弃消息。
 * 保留区也满时仍返回 NOT_READY，不承诺无限可靠排队。 */
class transport final {
public:
  explicit transport(std::size_t event_capacity = 256U, std::size_t write_capacity = 64U,
                     message_classifier is_discardable = nullptr);
  ~transport();

  transport(const transport&) = delete;
  transport& operator=(const transport&) = delete;

  [[nodiscard]] error start_server();
  [[nodiscard]] error start_client(const endpoint& endpoint);
  [[nodiscard]] error send(const envelope& envelope);
  [[nodiscard]] error stop();

  [[nodiscard]] std::size_t drain_events(std::vector<transport_event>& output,
                                         std::size_t max_count = 64U);
  [[nodiscard]] endpoint endpoint() const;
  [[nodiscard]] transport_state state() const noexcept;
  [[nodiscard]] std::size_t dropped_event_count() const noexcept;
  [[nodiscard]] std::size_t pending_write_count() const noexcept;

private:
  struct implementation;
  std::unique_ptr<implementation> implementation_;
};

} // namespace gneiss::ipc
