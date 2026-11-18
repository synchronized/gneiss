// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime/ipc/editor_ipc_event.hpp"
#include <gneiss/engine/error.hpp>

#include <gneiss/protocol/ipc_statistics_protocol.hpp>

namespace gneiss::host {

gneiss::error decode_runtime_statistics_event(const gneiss::ipc::envelope& envelope,
                                              runtime_ipc_event& output) noexcept {
  auto decoded = protocol::decode(statistics_snapshot, envelope);
  if (!decoded)
    return decoded.error().error;
  output = runtime_statistics_event{envelope.request_id, std::move(*decoded)};
  return error::success;
}

} // namespace gneiss::host
