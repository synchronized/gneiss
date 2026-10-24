// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "runtime_ipc_outbound.hpp"

#include "ipc_session_protocol.hpp"

namespace gneiss::host::runtime {

gneiss::engine::core::result make_session_hello_event(
    const std::string& token,
    const std::vector<gneiss::engine::platform::ipc_domain_capability>& domains,
    std::uint32_t request_id, gneiss::engine::platform::ipc_envelope& output) noexcept {
  return encode_ipc_session_hello({.token = token, .domains = domains}, false, request_id, output);
}

} // namespace gneiss::host::runtime
