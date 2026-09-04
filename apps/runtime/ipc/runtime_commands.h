// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_RUNTIME_IPC_RUNTIME_COMMANDS_H_
#define GNEISS_APPS_RUNTIME_IPC_RUNTIME_COMMANDS_H_

#include "ipc_router.h"
#include "runtime_command_context.h"

namespace gneiss::runtime_internal {

using runtime_command_router = ipc_operation_router<runtime_command_context>;

[[nodiscard]] result register_runtime_session_commands(runtime_command_router& router) noexcept;
[[nodiscard]] result register_runtime_control_commands(runtime_command_router& router) noexcept;
[[nodiscard]] result register_runtime_inspection_commands(runtime_command_router& router) noexcept;
[[nodiscard]] result register_runtime_property_commands(runtime_command_router& router) noexcept;
[[nodiscard]] result register_runtime_commands(runtime_command_router& router) noexcept;

} // namespace gneiss::runtime_internal

#endif
