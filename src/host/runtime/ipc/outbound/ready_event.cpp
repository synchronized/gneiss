// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/error.hpp>
#include "runtime_ipc_outbound.hpp"

#include <gneiss/protocol/ipc_control_protocol.hpp>

namespace gneiss::host {

gneiss::error make_ready_event(gneiss::ipc_envelope& output) noexcept {
  return encode_ipc_control_ready(output);
}

} // namespace gneiss::host
