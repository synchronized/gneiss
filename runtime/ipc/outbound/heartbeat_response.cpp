// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/error.hpp>
#include "ipc/outbound/runtime_ipc_outbound.hpp"

#include <gneiss/protocol/ipc_session_protocol.hpp>

namespace gneiss::host {

gneiss::error make_heartbeat_response(const ipc_session_heartbeat& heartbeat,
                                              std::uint32_t request_id,
                                              gneiss::ipc::envelope& output) {
  return encode_ipc_session_heartbeat(heartbeat, true, request_id, output);
}

} // namespace gneiss::host
