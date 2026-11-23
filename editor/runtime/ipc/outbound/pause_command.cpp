// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime/ipc/outbound/editor_ipc_outbound.hpp"
#include <gneiss/engine/core/base/error.hpp>

#include <gneiss/protocol/ipc_control_protocol.hpp>

namespace gneiss::host {

gneiss::error make_pause_command(std::uint32_t request_id,
                                         gneiss::ipc::envelope& output) noexcept {
  return encode_ipc_control_request(ipc_control_operation::pause, request_id, output);
}

} // namespace gneiss::host
