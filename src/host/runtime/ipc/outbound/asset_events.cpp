// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "runtime_ipc_outbound.hpp"

namespace gneiss::host::runtime {

gneiss::engine::core::result
make_asset_reload_result(const ipc_asset_reload_result& response, ipc_asset_operation operation,
                         std::uint32_t request_id,
                         gneiss::engine::platform::ipc_envelope& output) noexcept {
  return encode_ipc_asset_result_v2(response, operation, request_id, output);
}

} // namespace gneiss::host::runtime
