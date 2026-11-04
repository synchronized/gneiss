// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_RUNTIME_RUNTIME_IPC_SESSION_H_
#define GNEISS_APPS_RUNTIME_RUNTIME_IPC_SESSION_H_

#include "engine/core/error.hpp"
#include "ipc/runtime_command_context.hpp"

#include "ipc_inspection_protocol.hpp"
#include "ipc_property_protocol.hpp"
#include "ipc_protocol_domains.hpp"
#include "ipc_router.hpp"
#include "ipc_session_protocol.hpp"
#include "ipc_statistics_protocol.hpp"
#include "ipc_transport.hpp"

#include "engine/core/log/log_types.hpp"
#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace gneiss::host {

struct runtime_ipc_config final {
  gneiss::engine::ipc_endpoint endpoint;
  std::string session_token;
  std::chrono::milliseconds handshake_timeout{5000};
  std::chrono::milliseconds heartbeat_timeout{10000};
};

/** Runtime 主线程拥有的可选 IPC 会话；内部 Transport 的 socket 仅由 I/O 线程操作。 */
class runtime_ipc_session final {
public:
  using clock = std::chrono::steady_clock;

  explicit runtime_ipc_session(runtime_ipc_config config);
  ~runtime_ipc_session();

  runtime_ipc_session(const runtime_ipc_session&) = delete;
  runtime_ipc_session& operator=(const runtime_ipc_session&) = delete;

  [[nodiscard]] gneiss::engine::error start(clock::time_point now) noexcept;
  [[nodiscard]] gneiss::engine::error pump(clock::time_point now,
                                           runtime_ipc_actions& actions) noexcept;
  [[nodiscard]] gneiss::engine::error notify_running() noexcept;
  [[nodiscard]] gneiss::engine::error notify_scene_progress(const ipc_scene_progress& value,
                                                            std::uint32_t request_id) noexcept;
  void discard_pending_inspection() noexcept;
  [[nodiscard]] gneiss::engine::error notify_shutdown(std::int32_t exit_code) noexcept;
  [[nodiscard]] gneiss::engine::error
  notify_log_event(const gneiss::engine::event_view& event) noexcept;
  [[nodiscard]] gneiss::engine::error
  notify_scene_snapshot(const ipc_inspection_batch& batch) noexcept;
  [[nodiscard]] gneiss::engine::error
  notify_property_write_result(const ipc_property_write_result& response) noexcept;
  [[nodiscard]] gneiss::engine::error
  notify_asset_reload_result(const ipc_asset_reload_result& response, ipc_asset_operation operation,
                             std::uint32_t request_id) noexcept;
  [[nodiscard]] gneiss::engine::error
  notify_statistics(const ipc_runtime_statistics& statistics) noexcept;
  [[nodiscard]] std::size_t pending_write_count() const noexcept;
  [[nodiscard]] std::size_t dropped_event_count() const noexcept;
  [[nodiscard]] gneiss::engine::error stop() noexcept;

  [[nodiscard]] runtime_ipc_state state() const noexcept;
  [[nodiscard]] bool supports_domain(gneiss::engine::ipc_domain domain) const noexcept;

private:
  struct implementation;
  std::unique_ptr<implementation> implementation_;
};

} // namespace gneiss::host

#endif
