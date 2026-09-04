// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_ipc_outbound.h"

#include "ipc_session_protocol.h"

namespace gneiss::runtime_internal {

result make_shutdown_event(std::int32_t exit_code, ipc_envelope& output) noexcept {
  return encode_ipc_session_shutdown({.exit_code = exit_code}, output);
}

} // namespace gneiss::runtime_internal
