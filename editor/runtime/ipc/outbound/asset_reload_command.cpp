// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime/ipc/outbound/editor_ipc_outbound.hpp"
#include <gneiss/engine/error.hpp>

namespace gneiss::host {

gneiss::error make_asset_reload_command(const ipc_asset_reload_request& command,
                                                ipc_asset_operation operation,
                                                std::uint32_t request_id,
                                                gneiss::ipc_envelope& output) noexcept {
  return encode_ipc_asset_request_v2(command, operation, request_id, output);
}

} // namespace gneiss::host
