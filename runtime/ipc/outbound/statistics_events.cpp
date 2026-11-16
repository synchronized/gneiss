// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/error.hpp>
#include "ipc/outbound/runtime_ipc_outbound.hpp"

#include <gneiss/protocol/ipc_statistics_protocol.hpp>

namespace gneiss::host {

gneiss::error make_statistics_event(const ipc_runtime_statistics& statistics,
                                            gneiss::ipc_envelope& output) noexcept {
  return encode_ipc_statistics_v2(statistics, output);
}

} // namespace gneiss::host
