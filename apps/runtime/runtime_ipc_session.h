// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_RUNTIME_RUNTIME_IPC_SESSION_H_
#define GNEISS_APPS_RUNTIME_RUNTIME_IPC_SESSION_H_

#include "ipc_inspection_protocol.h"
#include "ipc_property_edit_protocol.h"
#include "ipc_protocol.h"
#include "ipc_protocol_domains.h"
#include "ipc_statistics_protocol.h"
#include "ipc_transport.h"

#include <chrono>
#include <gneiss/log.h>
#include <memory>
#include <string>
#include <vector>

namespace gneiss::runtime_internal {

struct runtime_ipc_config final {
  ipc_endpoint endpoint;
  std::string session_token;
  std::chrono::milliseconds handshake_timeout{5000};
  std::chrono::milliseconds heartbeat_timeout{10000};
};

enum class runtime_ipc_state : std::uint8_t {
  stopped,
  connecting,
  authenticating,
  running,
  paused,
  stopping,
  failed,
};

struct runtime_ipc_actions final {
  bool pause_game = false;
  bool resume_game = false;
  bool request_exit = false;
  bool request_inspection_resync = false;
  std::vector<ipc_property_write> property_writes;
  result failure = result::success;
};

/** Runtime 主线程拥有的可选 IPC 会话；内部 Transport 的 socket 仅由 I/O 线程操作。 */
class runtime_ipc_session final {
public:
  using clock = std::chrono::steady_clock;

  explicit runtime_ipc_session(runtime_ipc_config config);
  ~runtime_ipc_session();

  runtime_ipc_session(const runtime_ipc_session&) = delete;
  runtime_ipc_session& operator=(const runtime_ipc_session&) = delete;

  [[nodiscard]] result start(clock::time_point now) noexcept;
  [[nodiscard]] result pump(clock::time_point now, runtime_ipc_actions& actions) noexcept;
  [[nodiscard]] result notify_running() noexcept;
  [[nodiscard]] result notify_shutdown(std::int32_t exit_code) noexcept;
  [[nodiscard]] result notify_log_event(const gneiss_log_event& event) noexcept;
  [[nodiscard]] result notify_scene_snapshot(const ipc_inspection_batch& batch) noexcept;
  [[nodiscard]] result
  notify_property_write_result(const ipc_property_write_result& response) noexcept;
  [[nodiscard]] result notify_statistics(const ipc_runtime_statistics& statistics) noexcept;
  [[nodiscard]] std::size_t pending_write_count() const noexcept;
  [[nodiscard]] std::size_t dropped_event_count() const noexcept;
  [[nodiscard]] result stop() noexcept;

  [[nodiscard]] runtime_ipc_state state() const noexcept;
  [[nodiscard]] bool game_updates_enabled() const noexcept;
  [[nodiscard]] bool supports_domain(ipc_domain domain) const noexcept;

private:
  struct implementation;
  std::unique_ptr<implementation> implementation_;
};

} // namespace gneiss::runtime_internal

#endif
