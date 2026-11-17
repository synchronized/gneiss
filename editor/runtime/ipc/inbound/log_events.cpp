// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime/ipc/editor_ipc_event.hpp"
#include <gneiss/engine/error.hpp>

#include "runtime/ipc/inbound/event_decode.hpp"

namespace gneiss::host {

gneiss::error decode_runtime_log_event(const gneiss::ipc::envelope& envelope,
                                               runtime_ipc_event& output) {
  return gneiss::host::detail::decode_value<runtime_log_event>(
      envelope, output, [&](auto& value) { return decode_ipc_log_event(envelope, value); });
}

} // namespace gneiss::host
