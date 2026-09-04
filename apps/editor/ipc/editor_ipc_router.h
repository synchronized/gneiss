// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_IPC_EDITOR_IPC_ROUTER_H_
#define GNEISS_APPS_EDITOR_IPC_EDITOR_IPC_ROUTER_H_

#include "editor_ipc_event.h"

#include "ipc_protocol_domains.h"
#include "ipc_router.h"

#include <vector>

namespace gneiss::editor {

/** 组合 Runtime 到 Editor 的协议域，并输出强类型事件。 */
class editor_ipc_router final {
public:
  editor_ipc_router() noexcept;

  editor_ipc_router(const editor_ipc_router&) = delete;
  editor_ipc_router& operator=(const editor_ipc_router&) = delete;

  [[nodiscard]] result dispatch(const ipc_envelope& envelope, bool is_authenticated,
                                const std::vector<ipc_domain_capability>& negotiated_domains,
                                runtime_ipc_event& output) noexcept;
  [[nodiscard]] bool is_ready() const noexcept;

private:
  ipc_router<runtime_ipc_event> router_;
  bool is_ready_ = false;
};

} // namespace gneiss::editor

#endif
