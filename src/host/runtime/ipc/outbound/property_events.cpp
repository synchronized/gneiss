// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "runtime_ipc_outbound.hpp"

#include "ipc_property_protocol.hpp"

namespace gneiss::host::runtime {

gneiss::engine::core::result
make_property_write_result_event(const ipc_property_write_result& response,
                                 std::uint32_t request_id,
                                 gneiss::engine::platform::ipc_envelope& output) noexcept {
  return encode_ipc_property_result_v2(response, request_id, output);
}

} // namespace gneiss::host::runtime
