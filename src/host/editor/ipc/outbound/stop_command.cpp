// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "editor_ipc_outbound.hpp"
#include "engine/core/result.hpp"

#include "ipc_control_protocol.hpp"

namespace gneiss::host {

gneiss::engine::result make_stop_command(std::uint32_t request_id,
                                         gneiss::engine::ipc_envelope& output) noexcept {
  return encode_ipc_control_request(ipc_control_operation::stop, request_id, output);
}

} // namespace gneiss::host
