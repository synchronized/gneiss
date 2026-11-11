// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "editor_ipc_event.hpp"
#include "engine/core/error.hpp"

#include "event_decode.hpp"

namespace gneiss::host {

gneiss::error decode_runtime_log_event(const gneiss::ipc_envelope& envelope,
                                               runtime_ipc_event& output) {
  return gneiss::host::detail::decode_value<runtime_log_event>(
      envelope, output, [&](auto& value) { return decode_ipc_log_event(envelope, value); });
}

} // namespace gneiss::host
