// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "editor_ipc_outbound.h"

#include "ipc_control_protocol.h"

namespace gneiss::editor {

result make_resume_command(std::uint32_t request_id, ipc_envelope& output) noexcept {
  return encode_ipc_control_request(ipc_control_operation::resume, request_id, output);
}

} // namespace gneiss::editor
