// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/error.hpp>
#include "ipc/outbound/runtime_ipc_outbound.hpp"

#include <gneiss/protocol/ipc_session_protocol.hpp>

#include <utility>

namespace gneiss::host {

gneiss::error make_protocol_error_response(gneiss::error operation,
                                                   std::string message, std::uint32_t request_id,
                                                   gneiss::ipc::envelope& output) {
  return encode_ipc_session_error({.code = operation.code(), .message = std::move(message)},
                                  request_id, output);
}

} // namespace gneiss::host
