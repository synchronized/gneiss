// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_RUNTIME_IPC_RUNTIME_COMMAND_CONTEXT_H_
#define GNEISS_APPS_RUNTIME_IPC_RUNTIME_COMMAND_CONTEXT_H_

#include <gneiss/engine/error.hpp>
#include <gneiss/protocol/ipc_asset_protocol.hpp>
#include <gneiss/protocol/ipc_control_protocol.hpp>
#include <gneiss/protocol/ipc_property_protocol.hpp>
#include <gneiss/protocol/ipc_scene_protocol.hpp>
#include <gneiss/engine/platform/ipc/transport.hpp>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace gneiss::host {

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
  struct asset_reload_command final {
    ipc_asset_reload_request request;
    ipc_asset_operation operation{ipc_asset_operation::reload};
    std::uint32_t request_id = 0U;
  };
  std::vector<asset_reload_command> asset_reloads;
  std::vector<ipc_asset_reload_request> asset_cancels;
  struct scene_command {
    ipc_scene_request request;
    std::uint32_t request_id{};
    bool cancel{};
  };
  std::vector<scene_command> scene_commands;
  gneiss::error failure = gneiss::error::success;
};

/** 单条 Runtime IPC 命令可访问的最小会话能力，仅在主线程 dispatch 期间有效。 */
class runtime_command_context final {
public:
  using send_callback = gneiss::error (*)(void*, gneiss::ipc::envelope);

  runtime_command_context(runtime_ipc_state& state, runtime_ipc_actions& actions,
                          void* send_context, send_callback send) noexcept
      : state_(state), actions_(actions), send_context_(send_context), send_(send) {}

  [[nodiscard]] runtime_ipc_state state() const noexcept { return state_; }
  void set_state(runtime_ipc_state state) noexcept { state_ = state; }
  [[nodiscard]] runtime_ipc_actions& actions() noexcept { return actions_; }
  [[nodiscard]] gneiss::error send(gneiss::ipc::envelope envelope) {
    return send_ == nullptr ? gneiss::error::invalid_state
                            : send_(send_context_, std::move(envelope));
  }
  [[nodiscard]] gneiss::error send_state(ipc_control_state state);
  [[nodiscard]] gneiss::error send_protocol_error(gneiss::error operation,
                                                          std::string message,
                                                          std::uint32_t request_id);

private:
  runtime_ipc_state& state_;
  runtime_ipc_actions& actions_;
  void* send_context_ = nullptr;
  send_callback send_ = nullptr;
};

} // namespace gneiss::host

#endif
