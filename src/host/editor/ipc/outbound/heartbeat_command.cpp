// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "editor_ipc_outbound.hpp"
#include "engine/core/result.hpp"

#include "ipc_session_protocol.hpp"

namespace gneiss::host {

gneiss::engine::result make_heartbeat_command(std::uint64_t nonce, std::uint32_t request_id,
                                              gneiss::engine::ipc_envelope& output) noexcept {
  return encode_ipc_session_heartbeat({.nonce = nonce}, false, request_id, output);
}

} // namespace gneiss::host
