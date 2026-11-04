// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/error.hpp"
#include "runtime_ipc_outbound.hpp"

namespace gneiss::host {

gneiss::engine::error make_asset_reload_result(const ipc_asset_reload_result& response,
                                               ipc_asset_operation operation,
                                               std::uint32_t request_id,
                                               gneiss::engine::ipc_envelope& output) noexcept {
  return encode_ipc_asset_result_v2(response, operation, request_id, output);
}

} // namespace gneiss::host
