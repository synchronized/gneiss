// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_ipc_outbound.hpp"

#include "ipc_control_protocol.hpp"

namespace gneiss::runtime_internal {

result make_ready_event(ipc_envelope& output) noexcept { return encode_ipc_control_ready(output); }

} // namespace gneiss::runtime_internal
