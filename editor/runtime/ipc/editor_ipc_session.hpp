// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_IPC_EDITOR_IPC_SESSION_H_
#define GNEISS_APPS_EDITOR_IPC_EDITOR_IPC_SESSION_H_

#include "runtime/ipc/editor_ipc_event.hpp"
#include <gneiss/engine/core/base/error.hpp>

#include <gneiss/engine/function/ipc/transport.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gneiss::host {

/** 管理 Editor 端 IPC Server、握手、心跳和命令发送。 */
class editor_ipc_session final {
public:
  editor_ipc_session();
  ~editor_ipc_session();

  editor_ipc_session(const editor_ipc_session&) = delete;
  editor_ipc_session& operator=(const editor_ipc_session&) = delete;

  [[nodiscard]] gneiss::error start();
  [[nodiscard]] gneiss::error stop();
  [[nodiscard]] gneiss::error update(bool is_peer_running,
                                             std::vector<runtime_ipc_event>& output);

  [[nodiscard]] gneiss::error request_stop();
  [[nodiscard]] gneiss::error request_pause();
  [[nodiscard]] gneiss::error request_resume();
  [[nodiscard]] gneiss::error request_inspection_resync();
  [[nodiscard]] gneiss::error send_property_write(const ipc_property_write& command);
  [[nodiscard]] gneiss::error send_asset_cancel(std::uint64_t session,
                                                        std::uint64_t revision);
  [[nodiscard]] gneiss::error send_scene_load(const ipc_scene_request& value);
  [[nodiscard]] gneiss::error send_scene_cancel(const ipc_scene_request& value);
  [[nodiscard]] bool supports_scene_loading() const noexcept;
  [[nodiscard]] gneiss::error send_asset_reload(const ipc_asset_reload_request& command,
                                                        ipc_asset_operation operation);

  [[nodiscard]] bool is_authenticated() const noexcept;
  [[nodiscard]] bool supports_property_editing() const noexcept;
  [[nodiscard]] bool supports_asset_reload() const noexcept;
  [[nodiscard]] gneiss::ipc::endpoint endpoint() const;
  [[nodiscard]] const std::string& token() const noexcept;

private:
  struct implementation;
  std::unique_ptr<implementation> implementation_;
};

} // namespace gneiss::host

#endif
