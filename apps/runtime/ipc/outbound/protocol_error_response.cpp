// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_ipc_outbound.h"

#include "ipc_session_protocol.h"

#include <utility>

namespace gneiss::runtime_internal {

result make_protocol_error_response(result operation, std::string message, std::uint32_t request_id,
                                    ipc_envelope& output) noexcept {
  return encode_ipc_session_error({.code = to_native(operation), .message = std::move(message)},
                                  request_id, output);
}

} // namespace gneiss::runtime_internal
