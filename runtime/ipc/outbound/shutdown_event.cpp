// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/error.hpp>
#include "ipc/outbound/runtime_ipc_outbound.hpp"

#include <gneiss/protocol/ipc_session_protocol.hpp>

namespace gneiss::host {

gneiss::error make_shutdown_event(std::int32_t exit_code,
                                          gneiss::ipc::envelope& output) {
  return encode_ipc_session_shutdown({.exit_code = exit_code}, output);
}

} // namespace gneiss::host
