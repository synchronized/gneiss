// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "editor_ipc_commands.h"

#include "ipc_control_protocol.h"

namespace gneiss::editor {

result make_pause_command(std::uint32_t request_id, ipc_envelope& output) noexcept {
  return encode_ipc_control_request(ipc_control_operation::pause, request_id, output);
}

} // namespace gneiss::editor
