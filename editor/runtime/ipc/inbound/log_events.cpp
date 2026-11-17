// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime/ipc/editor_ipc_event.hpp"
#include <gneiss/engine/error.hpp>

#include "runtime/ipc/inbound/event_decode.hpp"

namespace gneiss::host {

gneiss::error decode_runtime_log_event(const gneiss::ipc::envelope& envelope,
                                       runtime_ipc_event& output) {
  auto decoded = protocol::decode(log_record, envelope);
  if (!decoded)
    return decoded.error().error;
  output = runtime_log_event{envelope.request_id, std::move(*decoded)};
  return error::success;
}

} // namespace gneiss::host
