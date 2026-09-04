// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_ipc_outbound.h"

namespace gneiss::runtime_internal {

result make_asset_reload_result(const ipc_asset_reload_result& response,
                                ipc_asset_operation operation, std::uint32_t request_id,
                                ipc_envelope& output) noexcept {
  return encode_ipc_asset_result_v2(response, operation, request_id, output);
}

} // namespace gneiss::runtime_internal
