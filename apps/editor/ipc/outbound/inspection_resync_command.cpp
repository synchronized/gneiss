// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "editor_ipc_outbound.h"

#include "ipc_inspection_protocol.h"

namespace gneiss::editor {

result make_inspection_resync_command(std::uint32_t request_id, ipc_envelope& output) noexcept {
  return encode_ipc_inspection_resync(request_id, output);
}

} // namespace gneiss::editor
