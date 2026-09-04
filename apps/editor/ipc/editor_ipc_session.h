// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_IPC_EDITOR_IPC_SESSION_H_
#define GNEISS_APPS_EDITOR_IPC_EDITOR_IPC_SESSION_H_

#include "editor_ipc_event.h"

#include "ipc_transport.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gneiss::editor {

/** 管理 Editor 端 IPC Server、握手、心跳和命令发送。 */
class editor_ipc_session final {
public:
  editor_ipc_session();
  ~editor_ipc_session();

  editor_ipc_session(const editor_ipc_session&) = delete;
  editor_ipc_session& operator=(const editor_ipc_session&) = delete;

  [[nodiscard]] result start() noexcept;
  [[nodiscard]] result stop() noexcept;
  [[nodiscard]] result update(bool is_peer_running,
                              std::vector<runtime_ipc_event>& output) noexcept;

  [[nodiscard]] result request_stop() noexcept;
  [[nodiscard]] result request_pause() noexcept;
  [[nodiscard]] result request_resume() noexcept;
  [[nodiscard]] result request_inspection_resync() noexcept;
  [[nodiscard]] result send_property_write(const ipc_property_write& command) noexcept;

  [[nodiscard]] bool is_authenticated() const noexcept;
  [[nodiscard]] bool supports_property_editing() const noexcept;
  [[nodiscard]] ipc_endpoint endpoint() const noexcept;
  [[nodiscard]] const std::string& token() const noexcept;

private:
  struct implementation;
  std::unique_ptr<implementation> implementation_;
};

} // namespace gneiss::editor

#endif
