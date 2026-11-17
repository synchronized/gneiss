// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/error.hpp>
#include "ipc/outbound/runtime_ipc_outbound.hpp"

#include <gneiss/protocol/ipc_session_protocol.hpp>

namespace gneiss::host {

gneiss::error
make_session_hello_event(const std::string& token,
                         const std::vector<gneiss::protocol::domain_capability>& domains,
                         std::uint32_t request_id, gneiss::ipc::envelope& output) {
  return encode_ipc_session_hello({.token = token, .domains = domains}, false, request_id, output);
}

} // namespace gneiss::host
