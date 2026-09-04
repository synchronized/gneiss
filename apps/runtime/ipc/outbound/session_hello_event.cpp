// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_ipc_outbound.h"

#include "ipc_session_protocol.h"

namespace gneiss::runtime_internal {

result make_session_hello_event(const std::string& token,
                                const std::vector<ipc_domain_capability>& domains,
                                std::uint32_t request_id, ipc_envelope& output) noexcept {
  return encode_ipc_session_hello({.token = token, .domains = domains}, false, request_id, output);
}

} // namespace gneiss::runtime_internal
