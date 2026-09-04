// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_ipc_outbound.h"

#include "ipc_property_protocol.h"

namespace gneiss::runtime_internal {

result make_property_write_result_event(const ipc_property_write_result& response,
                                        std::uint32_t request_id, ipc_envelope& output) noexcept {
  return encode_ipc_property_result_v2(response, request_id, output);
}

} // namespace gneiss::runtime_internal
