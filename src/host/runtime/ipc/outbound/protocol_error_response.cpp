// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "runtime_ipc_outbound.hpp"

#include "ipc_session_protocol.hpp"

#include <utility>

namespace gneiss::host::runtime {

gneiss::engine::core::result
make_protocol_error_response(gneiss::engine::core::result operation, std::string message,
                             std::uint32_t request_id,
                             gneiss::engine::platform::ipc_envelope& output) noexcept {
  return encode_ipc_session_error({.code = operation.code(), .message = std::move(message)},
                                  request_id, output);
}

} // namespace gneiss::host::runtime
