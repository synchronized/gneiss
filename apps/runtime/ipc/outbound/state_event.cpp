// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_ipc_outbound.h"

#include "ipc_control_protocol.h"

namespace gneiss::runtime_internal {

result make_state_event(ipc_control_state state, ipc_envelope& output) noexcept {
  return encode_ipc_control_state(state, output);
}

} // namespace gneiss::runtime_internal
