// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "runtime_ipc_outbound.hpp"

#include "ipc_log_protocol.hpp"

#include <gneiss/app/runtime_log_protocol.hpp>

#include <new>
#include <string>

namespace gneiss::host::runtime {

gneiss::engine::core::result
make_log_event(const gneiss_log_event& event,
               gneiss::engine::platform::ipc_envelope& output) noexcept {
  try {
    std::string encoded_event;
    const auto operation = app::encode_runtime_log_event(event, encoded_event);
    return operation == gneiss::engine::core::result::success
               ? encode_ipc_log_event(encoded_event, output)
               : operation;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::core::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::core::result::internal;
  }
}

} // namespace gneiss::host::runtime
