// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime/ipc/editor_ipc_event.hpp"
#include <gneiss/engine/error.hpp>

namespace gneiss::host {

gneiss::error decode_runtime_asset_event(const gneiss::ipc::envelope& envelope,
                                         runtime_ipc_event& output) noexcept {
  const auto& message = envelope.operation == asset_progress.operation        ? asset_progress
                        : envelope.operation == asset_reload_result.operation ? asset_reload_result
                                                                              : asset_resync_result;
  auto value = protocol::decode(message, envelope);
  if (!value)
    return value.error().error;
  output = runtime_asset_result_event{envelope.request_id, std::move(*value)};
  return error::success;
}

} // namespace gneiss::host
