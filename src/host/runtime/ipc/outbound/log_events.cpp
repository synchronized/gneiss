// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/error.hpp"
#include "runtime_ipc_outbound.hpp"

#include "ipc_log_protocol.hpp"

#include <gneiss/app/runtime_log_protocol.hpp>

#include <new>
#include <string>

namespace gneiss::host {

gneiss::engine::error make_log_event(const gneiss::engine::event_view& event,
                                     gneiss::engine::ipc_envelope& output) noexcept {
  try {
    std::string encoded_event;
    const auto operation = gneiss::host::encode_runtime_log_event(event, encoded_event);
    return operation == gneiss::engine::error::success ? encode_ipc_log_event(encoded_event, output)
                                                       : operation;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::error::out_of_memory;
  } catch (...) {
    return gneiss::engine::error::internal;
  }
}

} // namespace gneiss::host
