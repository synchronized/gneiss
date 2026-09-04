// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_RUNTIME_IPC_RUNTIME_COMMAND_CONTEXT_H_
#define GNEISS_APPS_RUNTIME_IPC_RUNTIME_COMMAND_CONTEXT_H_

#include "ipc_control_protocol.h"
#include "ipc_property_edit_protocol.h"
#include "ipc_transport.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace gneiss::runtime_internal {

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

/** 单条 Runtime IPC 命令可访问的最小会话能力，仅在主线程 dispatch 期间有效。 */
class runtime_command_context final {
public:
  using send_callback = result (*)(void*, ipc_envelope) noexcept;

  runtime_command_context(runtime_ipc_state& state, runtime_ipc_actions& actions,
                          void* send_context, send_callback send) noexcept
      : state_(state), actions_(actions), send_context_(send_context), send_(send) {}

  [[nodiscard]] runtime_ipc_state state() const noexcept { return state_; }
  void set_state(runtime_ipc_state state) noexcept { state_ = state; }
  [[nodiscard]] runtime_ipc_actions& actions() noexcept { return actions_; }
  [[nodiscard]] result send(ipc_envelope envelope) noexcept {
    return send_ == nullptr ? result::invalid_state : send_(send_context_, std::move(envelope));
  }
  [[nodiscard]] result send_state(ipc_control_state state) noexcept;
  [[nodiscard]] result send_protocol_error(result operation, std::string message,
                                           std::uint32_t request_id) noexcept;

private:
  runtime_ipc_state& state_;
  runtime_ipc_actions& actions_;
  void* send_context_ = nullptr;
  send_callback send_ = nullptr;
};

} // namespace gneiss::runtime_internal

#endif
