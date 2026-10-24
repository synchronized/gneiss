// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "runtime_ipc_outbound.hpp"

#include "ipc_control_protocol.hpp"

namespace gneiss::host {

gneiss::engine::result make_ready_event(gneiss::engine::ipc_envelope& output) noexcept {
  return encode_ipc_control_ready(output);
}

} // namespace gneiss::host
