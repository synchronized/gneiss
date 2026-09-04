// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_ipc_outbound.h"

#include "ipc_session_protocol.h"

namespace gneiss::runtime_internal {

result make_heartbeat_response(const ipc_session_heartbeat& heartbeat, std::uint32_t request_id,
                               ipc_envelope& output) noexcept {
  return encode_ipc_session_heartbeat(heartbeat, true, request_id, output);
}

} // namespace gneiss::runtime_internal
