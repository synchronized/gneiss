// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/error.hpp>
#include "ipc/outbound/runtime_ipc_outbound.hpp"

#include <gneiss/protocol/ipc_log_protocol.hpp>

#include <gneiss/protocol/runtime_log_protocol.hpp>

#include <new>
#include <string>

namespace gneiss::host {

gneiss::error make_log_event(const gneiss::logging::record_view& event,
                                     gneiss::ipc_envelope& output) {
  {
    std::string encoded_event;
    const auto operation = gneiss::host::encode_runtime_log_event(event, encoded_event);
    return operation == gneiss::error::success ? encode_ipc_log_event(encoded_event, output)
                                                       : operation;
  }
}

} // namespace gneiss::host
