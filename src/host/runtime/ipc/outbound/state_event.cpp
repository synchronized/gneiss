// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/error.hpp>
#include "runtime_ipc_outbound.hpp"

#include <gneiss/protocol/ipc_control_protocol.hpp>

namespace gneiss::host {

gneiss::error make_state_event(ipc_control_state state,
                                       gneiss::ipc_envelope& output) {
  return encode_ipc_control_state(state, output);
}

} // namespace gneiss::host
