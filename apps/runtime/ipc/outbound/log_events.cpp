// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_ipc_outbound.h"

#include "ipc_data_protocol.h"

#include <gneiss/app/runtime_log_protocol.h>

#include <new>
#include <string>

namespace gneiss::runtime_internal {

result make_log_event(const gneiss_log_event& event, ipc_envelope& output) noexcept {
  try {
    std::string encoded_event;
    const auto operation = app::encode_runtime_log_event(event, encoded_event);
    return operation == result::success ? encode_ipc_log_event(encoded_event, output) : operation;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

} // namespace gneiss::runtime_internal
