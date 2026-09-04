// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "editor_ipc_commands.h"

#include "ipc_session_protocol.h"

namespace gneiss::editor {

result make_heartbeat_command(std::uint64_t nonce, std::uint32_t request_id,
                              ipc_envelope& output) noexcept {
  return encode_ipc_session_heartbeat({.nonce = nonce}, false, request_id, output);
}

} // namespace gneiss::editor
